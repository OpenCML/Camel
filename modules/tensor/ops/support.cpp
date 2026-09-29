/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Operator plumbing implementation (see support.h).
 */

#include "support.h"

#include "../interop.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/array.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;

TensorObject *tensorArg(ArgsView &args, size_t index, bool allowArray) {
    Type *argType = args.type(index);
    if (asTensorType(argType)) {
        auto *tensor = args.get<TensorObject *>(index);
        if (!tensor) {
            throwRuntimeFault(RuntimeDiag::RuntimeError, "Tensor argument cannot be null");
        }
        return tensor;
    }
    if (allowArray && argType->code() == TypeCode::Array) {
        return tensorFromArray(args.slot(index), argType, resultAllocator());
    }
    throwRuntimeFault(RuntimeDiag::RuntimeError, "Expected Tensor argument");
}

kernels::Operand operandArg(ArgsView &args, size_t index, kernels::ScalarOperand &storage) {
    if (asTensorType(args.type(index)) || args.type(index)->code() == TypeCode::Array) {
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

slot_t wrap(TensorObject *tensor) {
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

type::Type *tensorOf(const TensorFacts &facts) { return TensorType::get(facts.dtype, facts.shape); }

type::Type *tensorOf(std::optional<TypeCode> dtype, std::optional<StaticShape> shape) {
    return TensorType::get(dtype, std::move(shape));
}

std::optional<TypeCode> promote(std::optional<TypeCode> lhs, std::optional<TypeCode> rhs) {
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    return promoteTensorTypes(*lhs, *rhs);
}

std::optional<StaticShape>
broadcast(const std::optional<StaticShape> &lhs, const std::optional<StaticShape> &rhs) {
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    const size_t rank = std::max(lhs->size(), rhs->size());
    StaticShape out(rank, 1);
    for (size_t i = 0; i < rank; ++i) {
        const int64_t a = i < rank - lhs->size() ? 1 : (*lhs)[i - (rank - lhs->size())];
        const int64_t b = i < rank - rhs->size() ? 1 : (*rhs)[i - (rank - rhs->size())];
        if (a == b) {
            out[i] = a;
        } else if (a == 1) {
            out[i] = b;
        } else if (b == 1) {
            out[i] = a;
        } else if (a == kUnknownDim || b == kUnknownDim) {
            // One side unknown, the other known and != 1: the result must be the known extent.
            out[i] = a == kUnknownDim ? b : a;
        } else {
            throw ShapeError(
                "cannot broadcast extents " + std::to_string(a) + " and " + std::to_string(b));
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
            return StaticShape(input->size(), kUnknownDim);
        }
        return input->empty()
                   ? std::nullopt
                   : std::optional<StaticShape>(StaticShape(input->size() - 1, kUnknownDim));
    }
    const auto rank = static_cast<int64_t>(input->size());
    int64_t a       = *axis < 0 ? *axis + rank : *axis;
    if (a < 0 || a >= rank) {
        throw std::invalid_argument("axis out of range");
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
        if (asTensorType(ctx.type(i))) {
            return true;
        }
    }
    return false;
}

bool anyTensorLike(const InferContext &ctx, size_t count) {
    for (size_t i = 0; i < count && i < ctx.size(); ++i) {
        if (asTensorType(ctx.type(i)) || ctx.type(i)->code() == TypeCode::Array) {
            return true;
        }
    }
    return false;
}

} // namespace camel::tensor::ops
