// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Operator plumbing implementation (see support.h).
 */

#include "support.h"

#include "../dtype.h"
#include "../interop.h"
#include "../tensor.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/assert.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;

Array *tensorArg(ArgsView &args, size_t index, bool allowArray) {
    Type *argType = args.type(index);
    if (isTensorType(argType)) {
        // A tensor is a numeric array (plan section 5.2): the value the argument holds is the
        // tensor itself, at whatever rank and element type it states. An array the module cannot
        // use as one — a nested array of arrays, which has to be flattened — is what the
        // conversion below is for.
        auto *tensor = args.get<Array *>(index);
        if (!tensor) {
            // The slot the type promises a pointer in holds the element itself when the value has
            // no dimension (the rank-zero ruling, docs/handover/reviews/questions-2026-10-23.md
            // section rank-zero-tensor): a rank-0 result stays an array only where the type it is
            // read by says so (wrap), so a value like this is a value with no dimension and not an
            // argument that went missing. The message says what the value is rather than calling it
            // a null argument (the author's ruling of 2026-10-25, section
            // no-dimension-value-in-an-array-typed-slot).
            throwRuntimeFault(
                RuntimeDiag::RuntimeError, "Tensor argument holds a value with no dimension");
        }
        return tensor;
    }
    if (allowArray && argType->code() == TypeCode::Array) {
        return tensorFromArray(args.slot(index), argType, resultAllocator());
    }
    // Rank zero is the element type itself (the rank-zero ruling,
    // docs/handover/reviews/questions-2026-10-23.md): the value of a rank-0 tensor is a scalar
    // slot, and the type says so — `f32` is `f32`, not an array. A kernel that reads
    // the tensor reads an array of no dimension, so the scalar is boxed here; wrap() takes the
    // same value back out of a kernel's result, where the declared result type says the element.
    // Without this a gradient handed to `tensor:sum_to` or a broadcast reached the kernel as the
    // scalar it is and was refused as "not a Tensor".
    if (isSupportedTensorScalar(argType->code())) {
        Array *box = createTensor(argType->code(), std::span<const int64_t>{}, resultAllocator());
        box->setSlot(
            0,
            args.slot(index),
            ArrayType::create(Type::create(argType->code()), std::span<const int64_t>{}));
        return box;
    }
    throwRuntimeFault(RuntimeDiag::RuntimeError, "Expected an array argument");
}

kernels::Operand operandArg(ArgsView &args, size_t index, kernels::ScalarOperand &storage) {
    if (args.type(index)->code() == TypeCode::Array) {
        return kernels::Operand::of(tensorArg(args, index, true));
    }
    storage = kernels::ScalarOperand::fromSlot(args.type(index)->code(), args.slot(index));
    return storage.view();
}

int64_t intArg(ArgsView &args, size_t index) {
    return args.type(index)->code() == TypeCode::Int32
               ? static_cast<int64_t>(args.get<camel::core::rtdata::Int32>(index))
               : args.get<camel::core::rtdata::Int64>(index);
}

double numberArg(ArgsView &args, size_t index) {
    return scalarToDouble(args.type(index)->code(), args.slot(index));
}

bool boolArg(ArgsView &args, size_t index) { return args.get<camel::core::rtdata::Bool>(index); }

std::string stringArg(ArgsView &args, size_t index) {
    auto *text = args.get<::String *>(index);
    return text ? text->toString() : std::string();
}

Shape shapeArg(ArgsView &args, size_t index) {
    return parseShapeArray(args.get<::Array *>(index), args.type(index));
}

slot_t wrap(type::Type *declared, Array *tensor) {
    // A result with no dimension is the element itself (the rank-zero ruling) — where the type
    // the result is read by says the element. When that type is an array, declared or with a rank
    // the compiler could not fix (`Tensor`, `f32[..]`), an array is what its readers take the
    // slot for, so a rank-0 array stays one: a consumer reading an array (tensorArg, a reducer,
    // the printer driven by the declared type) then finds the array its type promised, and only a
    // consumer reading the element finds the element.
    const bool element =
        declared != nullptr && declared->code() != camel::core::type::TypeCode::Array;
    if (element && tensor != nullptr && tensor->rank() == 0 && tensor->numel() == 1) {
        return tensor->getSlot(0);
    }
    return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Object *>(tensor));
}

mm::IAllocator &resultAllocator() { return mm::autoSpace(); }

void reportShapeError(std::string_view op, const char *detail) {
    throwRuntimeFault(RuntimeDiag::TensorDimensionMismatch, std::string(op), std::string(detail));
}

void reportKernelError(std::string_view op, const char *detail) {
    throwRuntimeFault(RuntimeDiag::RuntimeError, std::string(op) + ": " + detail);
}

std::optional<ConstArg> constArgOf(slot_t slot, type::Type *type) {
    if (!type) {
        return std::nullopt;
    }
    const TypeCode code = type->code();
    switch (code) {
    case TypeCode::Int32:
    case TypeCode::Int64:
        return ConstArg{scalarToInt64(code, slot)};
    case TypeCode::Float32:
    case TypeCode::Float64:
        return ConstArg{scalarToDouble(code, slot)};
    case TypeCode::Bool:
        return ConstArg{scalarToBool(code, slot)};
    case TypeCode::String:
        return ConstArg{rtdata::fromSlot<::String *>(slot)->toString()};
    case TypeCode::Array: {
        auto *elem = static_cast<ArrayType *>(type)->elemType();
        if (elem && (elem->code() == TypeCode::Int32 || elem->code() == TypeCode::Int64)) {
            return ConstArg{parseIntArray(rtdata::fromSlot<::Array *>(slot), type)};
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

std::optional<PartialInts> partialIntsOf(const type::StaticArg &arg, type::Type *type) {
    if (!arg.elements || !type || type->code() != TypeCode::Array) {
        return std::nullopt;
    }
    auto *elem = static_cast<ArrayType *>(type)->elemType();
    if (!elem || (elem->code() != TypeCode::Int32 && elem->code() != TypeCode::Int64)) {
        return std::nullopt;
    }
    PartialInts values;
    values.reserve(arg.elements->size());
    for (const auto &element : *arg.elements) {
        values.push_back(
            element ? std::optional<int64_t>(scalarToInt64(elem->code(), *element)) : std::nullopt);
    }
    return values;
}

StaticFacts::StaticFacts(std::span<type::Type *const> types, type::static_args_t statics)
    : constants(types.size()), partials(types.size()) {
    for (size_t i = 0; i < types.size() && i < statics.size(); ++i) {
        if (statics[i]) {
            constants[i] = constArgOf(*statics[i], types[i]);
        } else {
            partials[i] = partialIntsOf(statics[i], types[i]);
        }
    }
}

type::Type *tensorOf(const TensorFacts &facts) {
    // The type a value with these facts has: a numeric array of that element type and shape
    // (plan section 5.2). No dimension at all is the element type itself — the rank-zero ruling,
    // docs/handover/reviews/questions-2026-10-23.md, section rank-zero-tensor — and a rank
    // nobody fixed is the open-rank array.
    std::vector<ExtentForm> forms;
    if (facts.shape) {
        forms.reserve(facts.shape->size());
        for (const std::optional<int64_t> &extent : *facts.shape) {
            forms.push_back(extent ? ExtentForm::constant(*extent) : ExtentForm::unknown());
        }
    }
    // Every array a value can have states its element type (design principles 5.1), so a result
    // type built from the facts always has one: the element type is a precondition of this
    // function, not something a program can leave out.
    ASSERT(facts.dtype.has_value(), "a tensor's element type is always in its type");
    if (!isSupportedTensorScalar(*facts.dtype)) {
        throw std::invalid_argument("Unsupported array element type");
    }
    type::Type *elem = type::Type::create(*facts.dtype);
    if (!facts.shape) {
        return ArrayType::openRank(elem);
    }
    if (forms.empty()) {
        return elem;
    }
    return ArrayType::create(elem, std::span<const ExtentForm>(forms));
}

type::Type *tensorOf(std::optional<TypeCode> dtype, std::optional<StaticShape> shape) {
    return tensorOf(TensorFacts{dtype, std::move(shape)});
}

std::optional<TypeCode> promote(std::optional<TypeCode> lhs, std::optional<TypeCode> rhs) {
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    return promoteTensorTypes(*lhs, *rhs);
}

void refuseMixedFloatWidths(std::optional<TypeCode> lhs, std::optional<TypeCode> rhs) {
    if (!lhs || !rhs || *lhs == *rhs) {
        return;
    }
    const auto statedWidth = [](TypeCode code) {
        return code == TypeCode::Float32 || code == TypeCode::Float64;
    };
    if (!statedWidth(*lhs) || !statedWidth(*rhs)) {
        return;
    }
    throw std::invalid_argument(
        "the two tensor operands have different element types ('" + typeCodeToString(*lhs) +
        "' and '" + typeCodeToString(*rhs) +
        "'): a tensor operator computes in one element type, and there is no implicit conversion "
        "between numeric types (write the conversion, cast(value, \"" + typeCodeToString(*lhs) +
        "\") or cast(value, \"" + typeCodeToString(*rhs) + "\"))");
}

std::optional<StaticShape>
broadcast(const std::optional<StaticShape> &lhs, const std::optional<StaticShape> &rhs) {
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    const size_t rank = std::max(lhs->size(), rhs->size());
    StaticShape out(rank, 1);
    for (size_t i = 0; i < rank; ++i) {
        const std::optional<int64_t> a =
            i < rank - lhs->size() ? std::optional<int64_t>(1) : (*lhs)[i - (rank - lhs->size())];
        const std::optional<int64_t> b =
            i < rank - rhs->size() ? std::optional<int64_t>(1) : (*rhs)[i - (rank - rhs->size())];
        if (a == b) {
            out[i] = a;
        } else if (a == 1) {
            out[i] = b;
        } else if (b == 1) {
            out[i] = a;
        } else if (!a || !b) {
            // One side unknown, the other known and != 1: the result must be the known extent.
            out[i] = !a ? b : a;
        } else {
            throw ShapeError(
                "cannot broadcast extents " + std::to_string(*a) + " and " + std::to_string(*b));
        }
    }
    return out;
}

std::optional<StaticShape>
reduceShape(const std::optional<StaticShape> &input, std::optional<int64_t> axis, bool keepDims) {
    if (!input) {
        return std::nullopt;
    }
    if (!axis) {
        if (keepDims) {
            return StaticShape(input->size(), std::nullopt);
        }
        return input->empty()
                   ? std::nullopt
                   : std::optional<StaticShape>(StaticShape(input->size() - 1, std::nullopt));
    }
    const auto rank = static_cast<int64_t>(input->size());
    int64_t a       = *axis < 0 ? *axis + rank : *axis;
    if (a < 0 || a >= rank) {
        throw ShapeError(std::format(
            "axis {} is out of range for the rank-{} input {}",
            *axis,
            rank,
            formatShape(*input)));
    }
    StaticShape out;
    for (int64_t d = 0; d < rank; ++d) {
        if (d == a) {
            if (keepDims) {
                out.push_back(1);
            }
            continue;
        }
        out.push_back((*input)[static_cast<size_t>(d)]);
    }
    return out;
}

bool anyTensor(const InferContext &ctx, size_t count) {
    for (size_t i = 0; i < count && i < ctx.size(); ++i) {
        if (isTensorType(ctx.type(i))) {
            return true;
        }
    }
    if (ctx.sourceLevel()) {
        return false;
    }
    // A rank-0 tensor's static type is the element type itself (the rank-zero ruling,
    // docs/handover/reviews/questions-2026-10-23.md), so a type alone cannot tell one from a
    // scalar -- and `tensorArg` boxes exactly that scalar slot, a kernel reading the tensor as an
    // array of no dimension. Reading a graph the compiler already built (a pass, the export, a
    // draft re-inference) the node itself answers the question the type cannot: it is a `tensor:`
    // operator, so its numeric scalar operands are the rank-0 tensors its kernel takes, and
    // answering "no tensor" here is what left such a node with no dtype and no shape. At source
    // level the same call is not a tensor operation at all, and the scalar operator is the
    // program's own choice; the question stays where it belongs.
    for (size_t i = 0; i < count && i < ctx.size(); ++i) {
        Type *type = ctx.type(i);
        if (type != nullptr && isSupportedTensorScalar(type->code())) {
            return true;
        }
    }
    return false;
}

bool anyTensorLike(const InferContext &ctx, size_t count) {
    for (size_t i = 0; i < count && i < ctx.size(); ++i) {
        if (ctx.type(i)->code() == TypeCode::Array) {
            return true;
        }
    }
    return false;
}

std::string formatShape(std::span<const std::optional<int64_t>> shape) {
    std::string out = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        out += i ? ", " : "";
        out += shape[i] ? std::to_string(*shape[i]) : std::string("?");
    }
    return out + "]";
}

std::string formatShape(std::span<const int64_t> shape) {
    const std::vector<std::optional<int64_t>> known(shape.begin(), shape.end());
    return formatShape(std::span<const std::optional<int64_t>>(known));
}

} // namespace camel::tensor::ops
