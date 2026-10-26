// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Optimizers over parameter trees (see optim.h).
 */

#include "optim.h"

#include "state.h"

#include "../tensor/dtype.h"
#include "../tensor/kernels/elementwise.h"
#include "../tensor/kernels/update.h"
#include "../tensor/npy.h"
#include "../tensor/tensor.h"
#include "../tensor/type.h"

#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <span>

namespace camel::nn {

namespace {

namespace mm = camel::core::mm;
namespace k  = camel::tensor::kernels;
using camel::core::aggregateElement;
using camel::core::aggregateSize;
using camel::core::gradientElementIndex;
using camel::core::gradientTypeOf;
using camel::core::isAggregate;
using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::Float64;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Int64;
using camel::core::rtdata::toSlot;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::type_vec_t;
using camel::core::type::TypeCode;
using camel::tensor::Array;
using camel::tensor::createTensor;
using camel::tensor::viewTensor;

// ---------------------------------------------------------------- tree walking

/// Updates one trainable leaf. `in` holds the leaf of the primal tree followed by the leaves of
/// the tangent-shaped trees; `out` receives the new primal leaf followed by new tangent leaves.
using LeafUpdate =
    std::function<void(Type *leafType, std::span<const slot_t> in, std::span<slot_t> out)>;

camel::core::rtdata::Object *newAggregate(Type *type) {
    const size_t size = aggregateSize(type);
    if (type->code() == TypeCode::Struct) {
        return ::Struct::create(size, mm::autoSpace());
    }
    return ::Tuple::create(size, mm::autoSpace());
}

slot_t elementOf(Type *type, slot_t value, size_t index) {
    if (type->code() == TypeCode::Array) {
        return fromSlot<::Array *>(value)->get<slot_t>(index);
    }
    if (type->code() == TypeCode::Struct) {
        return fromSlot<::Struct *>(value)->get<slot_t>(index);
    }
    return fromSlot<::Tuple *>(value)->get<slot_t>(index);
}

void setElement(Type *type, slot_t aggregate, size_t index, slot_t value) {
    if (type->code() == TypeCode::Array) {
        fromSlot<::Array *>(aggregate)->set<slot_t>(
            index, value, tt::as_ptr<camel::core::type::ArrayType>(type));
        return;
    }
    if (type->code() == TypeCode::Struct) {
        fromSlot<::Struct *>(aggregate)->set<slot_t>(
            index,
            value,
            tt::as_ptr<camel::core::type::StructType>(type));
    } else {
        fromSlot<::Tuple *>(aggregate)->set<slot_t>(index, value, tt::as_ptr<TupleType>(type));
    }
}

/// A trainable leaf that is a tensor, in whichever spelling its type uses (`f32[4, 4]`, `f32[..]`,
/// `f32[]`): a numeric array is one tensor, not an array of its elements.
bool isTensorLeaf(Type *type) { return camel::tensor::isTensorType(type); }

/// Rebuilds one array of a trainable tree, element by element and with the length the primal
/// value has. An array is walked like a record: its tangent is the array of its elements'
/// tangents (design principles 9.9), so `blocks.0.wq` and `blocks.1.wq` are separate leaves.
void walk(Type *type, std::span<const slot_t> in, std::span<slot_t> out, const LeafUpdate &leaf);

void walkArray(
    Type *type, std::span<const slot_t> in, std::span<slot_t> out, const LeafUpdate &leaf) {
    auto *arrayType     = tt::as_ptr<camel::core::type::ArrayType>(type);
    Type *const tangent = gradientTypeOf(type);
    if (tangent == nullptr) {
        throw std::invalid_argument("the optimizer was given an array without a tangent");
    }
    auto *const primal     = fromSlot<::Array *>(in[0]);
    const size_t length    = primal->size();
    auto *tangentArrayType = tt::as_ptr<camel::core::type::ArrayType>(tangent);
    out[0]                 = toSlot<::Array *>(::Array::create(mm::autoSpace(), length));
    for (size_t t = 1; t < out.size(); ++t) {
        out[t] = toSlot<::Array *>(::Array::create(mm::autoSpace(), length));
    }
    std::vector<slot_t> childIn(in.size()), childOut(out.size());
    for (size_t i = 0; i < length; ++i) {
        childIn[0] = primal->get<slot_t>(i);
        for (size_t t = 1; t < in.size(); ++t) {
            childIn[t] = fromSlot<::Array *>(in[t])->get<slot_t>(i);
        }
        walk(arrayType->elemType(), childIn, childOut, leaf);
        fromSlot<::Array *>(out[0])->set<slot_t>(i, childOut[0], arrayType);
        for (size_t t = 1; t < out.size(); ++t) {
            fromSlot<::Array *>(out[t])->set<slot_t>(i, childOut[t], tangentArrayType);
        }
    }
}

/// Walks a primal tree of type `type` together with tangent-shaped trees, rebuilding all of
/// them with `leaf` applied to every trainable leaf.
void walk(Type *type, std::span<const slot_t> in, std::span<slot_t> out, const LeafUpdate &leaf) {
    // A tensor is one trainable leaf whichever spelling its type uses, and the test comes first:
    // `Tensor` and `f32[m, n]` are both numeric arrays (`isTensorLeaf`), and the transitional
    // `Tensor` carries the code of a native array too, so asking by code alone took a model's
    // tensor for an array of tensors and walked the elements of the leaf as if each were a leaf.
    // The parameter then came back as a length-1-extent array with no element type at all -- a
    // `[null, null]` value whose next `matmul`/`to_bytes` refused -- because `walkArray` builds its
    // result with `Array::create(mm::autoSpace(), length)`, the slot-array convenience that states
    // no element type (see `saveLeaves` below, which asks this question in this order).
    if (isTensorLeaf(type)) {
        leaf(type, in, out);
        return;
    }
    if (type->code() == TypeCode::Array) {
        walkArray(type, in, out, leaf);
        return;
    }
    if (!isAggregate(type)) {
        leaf(type, in, out);
        return;
    }
    Type *tangent = gradientTypeOf(type);
    out[0]        = toSlot<camel::core::rtdata::Object *>(newAggregate(type));
    for (size_t t = 1; t < out.size(); ++t) {
        out[t] = toSlot<camel::core::rtdata::Object *>(newAggregate(tangent));
    }
    std::vector<slot_t> childIn(in.size()), childOut(out.size());
    for (size_t i = 0; i < aggregateSize(type); ++i) {
        Type *elemType      = aggregateElement(type, i);
        const auto position = gradientElementIndex(type, i);
        if (!position) {
            setElement(type, out[0], i, elementOf(type, in[0], i)); // not trainable: kept
            continue;
        }
        childIn[0] = elementOf(type, in[0], i);
        for (size_t t = 1; t < in.size(); ++t) {
            childIn[t] = elementOf(tangent, in[t], *position);
        }
        walk(elemType, childIn, childOut, leaf);
        setElement(type, out[0], i, childOut[0]);
        for (size_t t = 1; t < out.size(); ++t) {
            setElement(tangent, out[t], *position, childOut[t]);
        }
    }
}

// ---------------------------------------------------------------- leaves

/// The element type of a tensor leaf: the one its value has. It is read, never converted: a leaf
/// whose type states an element type and its value always agree (the slot refuses a value of
/// another type), and a leaf whose type states none has its element type written in its value
/// alone -- `full([3, 2], 0.1)` is an f64 tensor, and "unstated" means the type does not say it,
/// not that the value is f32. A written element type is kept exactly and is never narrowed either (design
/// principles 7.4: the element type is exact; computing at a lower precision than the program
/// wrote would need the `change_precision` permission of 7.2-7.3).
TypeCode leafElementType(const Array *t) {
    const TypeCode dtype = t->dtype();
    if (dtype != TypeCode::Float32 && dtype != TypeCode::Float64) {
        // An element type that is not a floating one has no tangent (`tensorTangentType`), so no
        // such leaf reaches an optimizer. Naming it instead of converting it keeps the rule in one
        // place.
        throw std::invalid_argument(std::format(
            "a tensor leaf of element type {} is not trainable",
            camel::tensor::elementTypeSpelling(dtype)));
    }
    return dtype;
}

/// The leaf's tensor, as its value is: nothing converts it on the way in, and the pointer this
/// returns is read at the value's own element type by every caller (`dataAs<float>` only for an f32
/// leaf), so the bytes of an f64 leaf are never copied through a float local.
const Array *leafTensor(slot_t slot) { return fromSlot<Array *>(slot); }

/// A fresh tensor with `t`'s shape and `t`'s element type: `t` is the leaf's own value
/// (`leafTensor`), and an output of another type would change the leaf's element type on the way
/// out.
Array *newLike(const Array *t) {
    return createTensor(t->dtype(), t->shapeSpan(), mm::autoSpace());
}

void requireSameShape(const Array *param, const Array *grad, const char *op) {
    if (!param->sameShape(grad)) {
        throw std::invalid_argument(
            std::format("{}: gradient shape differs from its parameter", op));
    }
}

double floatValue(Type *type, slot_t slot) {
    return type->code() == TypeCode::Float32 ? fromSlot<float>(slot) : fromSlot<Float64>(slot);
}

slot_t floatSlot(Type *type, double value) {
    return type->code() == TypeCode::Float32 ? toSlot<float>(static_cast<float>(value))
                                             : toSlot<Float64>(value);
}

LeafUpdate sgdLeaf(double lr) {
    return [lr](Type *type, std::span<const slot_t> in, std::span<slot_t> out) {
        if (!isTensorLeaf(type)) {
            out[0] = floatSlot(type, floatValue(type, in[0]) - lr * floatValue(type, in[1]));
            return;
        }
        const Array *p = leafTensor(in[0]);
        const Array *g = leafTensor(in[1]);
        requireSameShape(p, g, "sgd");
        Array *next = newLike(p);
        // One step per element at the leaf's own element type, with the rate rounded to it: an f32
        // leaf keeps the f32 arithmetic it had, and an f64 leaf -- a written one or one whose type
        // states no element type and whose value is f64 -- keeps the f64 one (no float local stands
        // between the leaf's bytes and the result).
        if (p->dtype() == TypeCode::Float32) {
            const float *pp = p->dataAs<float>(), *pg = g->dataAs<float>();
            float *pn       = next->dataAs<float>();
            const auto rate = static_cast<float>(lr);
            for (uint64_t i = 0; i < p->numel(); ++i) {
                pn[i] = pp[i] - rate * pg[i];
            }
        } else {
            const double *pp = p->dataAs<double>(), *pg = g->dataAs<double>();
            double *pn       = next->dataAs<double>();
            for (uint64_t i = 0; i < p->numel(); ++i) {
                pn[i] = pp[i] - lr * pg[i];
            }
        }
        out[0] = toSlot<Array *>(next);
    };
}

struct AdamConfig {
    double lr, beta1, beta2, eps;
    int64_t step; // after this update, starting at 1
};

/// in: param, grad, m, v; out: param, m, v.
LeafUpdate adamLeaf(const AdamConfig &c) {
    // The formula lives in the tensor kernel (kernels/update.h): one definition, used both by the
    // fused pass over a tensor leaf and by the scalar path below.
    const k::AdaptiveUpdateRates rates{
        .lr       = c.lr,
        .beta1    = c.beta1,
        .beta2    = c.beta2,
        .eps      = c.eps,
        .correct1 = 1.0 - std::pow(c.beta1, static_cast<double>(c.step)),
        .correct2 = 1.0 - std::pow(c.beta2, static_cast<double>(c.step)),
    };
    return [rates](Type *type, std::span<const slot_t> in, std::span<slot_t> out) {
        if (!isTensorLeaf(type)) {
            double np, nm, nv;
            k::adaptiveUpdateOne(
                rates,
                floatValue(type, in[0]),
                floatValue(type, in[1]),
                floatValue(type, in[2]),
                floatValue(type, in[3]),
                np,
                nm,
                nv);
            out[0] = floatSlot(type, np);
            out[1] = floatSlot(type, nm);
            out[2] = floatSlot(type, nv);
            return;
        }
        const Array *p = leafTensor(in[0]);
        const Array *g = leafTensor(in[1]);
        const Array *m = leafTensor(in[2]);
        const Array *v = leafTensor(in[3]);
        requireSameShape(p, g, "adam");
        requireSameShape(p, m, "adam");
        requireSameShape(p, v, "adam");
        Array *np = newLike(p), *nm = newLike(p), *nv = newLike(p);
        // One pass over whole tensors, in double per element, split by the tensor module's grain
        // policy; the values are bit for bit those of the element-by-element loop it replaces. The
        // leaf's own element type decides which pass and which buffers: the f32 one computes in
        // double and rounds once on the store, and the f64 one keeps the double result, so neither
        // narrows the leaf (the accumulation width is the design's, the value's type is the
        // program's).
        if (p->dtype() == TypeCode::Float32) {
            k::adaptiveUpdate(
                static_cast<int64_t>(p->numel()),
                rates,
                p->dataAs<float>(),
                g->dataAs<float>(),
                m->dataAs<float>(),
                v->dataAs<float>(),
                np->dataAs<float>(),
                nm->dataAs<float>(),
                nv->dataAs<float>());
        } else {
            k::adaptiveUpdate64(
                static_cast<int64_t>(p->numel()),
                rates,
                p->dataAs<double>(),
                g->dataAs<double>(),
                m->dataAs<double>(),
                v->dataAs<double>(),
                np->dataAs<double>(),
                nm->dataAs<double>(),
                nv->dataAs<double>());
        }
        out[0] = toSlot<Array *>(np);
        out[1] = toSlot<Array *>(nm);
        out[2] = toSlot<Array *>(nv);
    };
}

/// in: param; out: param (unchanged), zero, zero.
void zerosLeaf(Type *type, std::span<const slot_t> in, std::span<slot_t> out) {
    out[0] = in[0];
    if (!isTensorLeaf(type)) {
        out[1] = out[2] = floatSlot(type, 0.0);
        return;
    }
    const auto *p = fromSlot<Array *>(in[0]);
    // The moments are tensors of the leaf's own element type -- the value's (`leafElementType`) --
    // so an f64 leaf's moments are f64 and a step of it stays f64 (an f32 moment would move the
    // parameter, which is what the guard test/cases/modules/optim/native_dtype_exact.cml pins), and
    // a leaf whose type states no element type gets the moments its value asks for.
    const TypeCode dtype = leafElementType(p);
    for (size_t t = 1; t <= 2; ++t) {
        out[t] = toSlot<Array *>(createTensor(dtype, p->shapeSpan(), mm::autoSpace(), true));
    }
}

// ---------------------------------------------------------------- kernels

template <typename Fn> slot_t runOptimizer(const char *name, Fn &&body) {
    try {
        return body();
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::format("nn.{}: {}", name, e.what()));
    }
}

double numberArg(ArgsView &norm, size_t index) {
    return floatValue(norm.type(index), norm.get<slot_t>(index));
}

slot_t sgdKernel(ArgsView &, ArgsView &norm, Context &) {
    return runOptimizer("sgd", [&] {
        const std::array<slot_t, 2> in{norm.get<slot_t>(0), norm.get<slot_t>(1)};
        std::array<slot_t, 1> out{};
        walk(norm.type(0), in, out, sgdLeaf(numberArg(norm, 2)));
        return out[0];
    });
}

/// Adam's trees: first and second moments, shaped like the gradient of `params`.
TupleType *momentsType(Type *params) {
    Type *tangent = gradientTypeOf(params);
    return TupleType::create(std::vector<Type *>{tangent, tangent});
}

slot_t adamStateKernel(ArgsView &, ArgsView &norm, Context &) {
    return runOptimizer("adam_state", [&] {
        Type *type = norm.type(0);
        const std::array<slot_t, 1> in{norm.get<slot_t>(0)};
        std::array<slot_t, 3> out{};
        walk(type, in, out, zerosLeaf);
        TupleType *treesType = momentsType(type);
        auto *trees          = ::Tuple::create(2, mm::autoSpace());
        trees->set<slot_t>(0, out[1], treesType);
        trees->set<slot_t>(1, out[2], treesType);
        return toSlot<OptimizerStateObject *>(
            OptimizerStateObject::create(trees, treesType, 0, mm::autoSpace()));
    });
}

// adam(params, grads, state, lr, beta1?, beta2?, eps?)
slot_t adamKernel(ArgsView &, ArgsView &norm, Context &) {
    return runOptimizer("adam", [&] {
        Type *type           = norm.type(0);
        auto *state          = norm.get<OptimizerStateObject *>(2);
        TupleType *treesType = momentsType(type);
        // Passes may sharpen the model type (a static tensor shape) on one side only, so the
        // two must be compatible, not identical.
        if (!state->treesType()->assignableFrom(treesType) &&
            !treesType->assignableFrom(state->treesType())) {
            throw std::invalid_argument("the state was created for a model of another shape");
        }
        const AdamConfig c{
            .lr    = numberArg(norm, 3),
            .beta1 = norm.size() > 4 ? numberArg(norm, 4) : 0.9,
            .beta2 = norm.size() > 5 ? numberArg(norm, 5) : 0.999,
            .eps   = norm.size() > 6 ? numberArg(norm, 6) : 1e-8,
            .step  = state->step() + 1,
        };
        const std::array<slot_t, 4> in{
            norm.get<slot_t>(0),
            norm.get<slot_t>(1),
            state->trees()->get<slot_t>(0),
            state->trees()->get<slot_t>(1)};
        std::array<slot_t, 3> out{};
        walk(type, in, out, adamLeaf(c));

        auto *trees = ::Tuple::create(2, mm::autoSpace());
        trees->set<slot_t>(0, out[1], treesType);
        trees->set<slot_t>(1, out[2], treesType);
        auto *next = OptimizerStateObject::create(trees, treesType, c.step, mm::autoSpace());
        TupleType *resultType =
            TupleType::create(std::vector<Type *>{type, OptimizerStateType::Default()});
        auto *result = ::Tuple::create(2, mm::autoSpace());
        result->set<slot_t>(0, out[0], resultType);
        result->set<slot_t>(1, toSlot<OptimizerStateObject *>(next), resultType);
        return toSlot<::Tuple *>(result);
    });
}

// ---------------------------------------------------------------- checkpoints

/// Saves every tensor leaf of `value` as <dir>/<path>.npy, the path joining struct field names
/// and tuple indices with dots.
void saveLeaves(
    Type *type, slot_t value, const std::filesystem::path &dir, const std::string &path) {
    if (isTensorLeaf(type)) {
        camel::tensor::saveNpy(fromSlot<Array *>(value), dir / (path + ".npy"));
        return;
    }
    if (type->code() == TypeCode::Array) {
        // An array's elements are named by their index, like a tuple's: `blocks.0.wq` and
        // `blocks.1.wq` are the two blocks, whichever the model calls them.
        auto *arrayType = tt::as_ptr<camel::core::type::ArrayType>(type);
        auto *array     = fromSlot<::Array *>(value);
        for (size_t i = 0; i < array->size(); ++i) {
            const std::string name = std::to_string(i);
            saveLeaves(
                arrayType->elemType(),
                array->get<slot_t>(i),
                dir,
                path.empty() ? name : path + "." + name);
        }
        return;
    }
    if (!isAggregate(type)) {
        return; // scalars and other members are not tensors
    }
    for (size_t i = 0; i < aggregateSize(type); ++i) {
        const std::string name =
            type->code() == TypeCode::Struct
                ? std::string(tt::as_ptr<camel::core::type::StructType>(type)->fieldName(i))
                : std::to_string(i);
        saveLeaves(
            aggregateElement(type, i),
            elementOf(type, value, i),
            dir,
            path.empty() ? name : path + "." + name);
    }
}

// save_params(tree, dir)
slot_t saveParamsKernel(ArgsView &, ArgsView &norm, Context &) {
    return runOptimizer("save_params", [&] {
        const std::filesystem::path dir(norm.get<::String *>(1)->toString());
        std::filesystem::create_directories(dir);
        saveLeaves(norm.type(0), norm.get<slot_t>(0), dir, "");
        return NullSlot;
    });
}

// ---------------------------------------------------------------- types

bool isNumber(Type *t) {
    return t->code() == TypeCode::Float64 || t->code() == TypeCode::Float32 ||
           t->code() == TypeCode::Int64 || t->code() == TypeCode::Int32;
}

/// Whether `grads` can be the gradient of `params`.
/// Whether `grads` can be the gradient of `params`. Static facts (e.g. tensor shapes inferred for
/// a literal model) are ignored: the gradient is typed by the declared parameter types.
bool gradientOf(Type *params, Type *grads) {
    Type *tangent = gradientTypeOf(params);
    return tangent != nullptr && tangent->widened()->equals(grads->widened());
}

class OptimizerResolver final : public camel::core::type::FuncTypeResolver {
  public:
    enum class Kind { Sgd, AdamState, Adam };
    explicit OptimizerResolver(Kind kind) : kind_(kind) {}

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        if (!with.empty() || norm.empty() || gradientTypeOf(norm[0]) == nullptr) {
            return std::nullopt;
        }
        Type *params = norm[0];
        camel::core::type::param_vec_t args;
        for (Type *t : norm) {
            args.emplace_back(t, false);
        }
        switch (kind_) {
        case Kind::Sgd:
            if (norm.size() != 3 || !gradientOf(params, norm[1]) || !isNumber(norm[2])) {
                return std::nullopt;
            }
            return FunctionType::create({}, args, params);
        case Kind::AdamState:
            if (norm.size() != 1) {
                return std::nullopt;
            }
            return FunctionType::create({}, args, OptimizerStateType::Default());
        case Kind::Adam: {
            if (norm.size() < 4 || norm.size() > 7 || !gradientOf(params, norm[1]) ||
                !OptimizerStateType::Default()->assignableFrom(norm[2])) {
                return std::nullopt;
            }
            for (size_t i = 3; i < norm.size(); ++i) {
                if (!isNumber(norm[i])) {
                    return std::nullopt;
                }
            }
            return FunctionType::create(
                {},
                args,
                TupleType::create(std::vector<Type *>{params, OptimizerStateType::Default()}));
        }
        }
        return std::nullopt;
    }

    std::string signature() const override {
        switch (kind_) {
        case Kind::Sgd:
            return "(params: P, grads: grad of P, lr: float) => P";
        case Kind::AdamState:
            return "(params: P) => OptimizerState";
        case Kind::Adam:
            return "(params: P, grads: grad of P, state: OptimizerState, lr: float, beta1?, "
                   "beta2?, eps?) => (P, OptimizerState)";
        }
        return "";
    }

  private:
    Kind kind_;
};

} // namespace

const std::vector<oper_group_ptr_t> &optimizerOperatorGroups() {
    using Kind                                        = OptimizerResolver::Kind;
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create("sgd", {{"nn:sgd", std::make_shared<OptimizerResolver>(Kind::Sgd)}}),
        OperatorGroup::create(
            "adam_state",
            {{"nn:adam_state", std::make_shared<OptimizerResolver>(Kind::AdamState)}}),
        OperatorGroup::create(
            "adam",
            {{"nn:adam", std::make_shared<OptimizerResolver>(Kind::Adam)}}),
        // `save_params` is not here: its signature is declared in `stdlib/nn.cml`, and C++ keeps
        // only its kernel (`optimizerKernels`) and its resource and traits (`NnModule::load`).
    };
    return groups;
}

std::unordered_map<std::string, operator_t> optimizerKernels() {
    return {
        {"sgd", &sgdKernel},
        {"adam_state", &adamStateKernel},
        {"adam", &adamKernel},
        {"save_params", &saveParamsKernel},
    };
}

} // namespace camel::nn
