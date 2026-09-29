/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Optimizers over parameter trees (see optim.h).
 */

#include "optim.h"

#include "state.h"

#include "../tensor/kernels/elementwise.h"
#include "../tensor/tensor.h"
#include "../tensor/type.h"

#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <cmath>
#include <format>
#include <functional>
#include <span>

namespace camel::nn {

namespace {

namespace mm = camel::core::mm;
namespace k  = camel::tensor::kernels;
using camel::core::aggregateElement;
using camel::core::aggregateSize;
using camel::core::isAggregate;
using camel::core::tangentElementIndex;
using camel::core::tangentTypeOf;
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
using camel::tensor::TensorObject;

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
    if (type->code() == TypeCode::Struct) {
        return fromSlot<::Struct *>(value)->get<slot_t>(index);
    }
    return fromSlot<::Tuple *>(value)->get<slot_t>(index);
}

void setElement(Type *type, slot_t aggregate, size_t index, slot_t value) {
    if (type->code() == TypeCode::Struct) {
        fromSlot<::Struct *>(aggregate)->set<slot_t>(
            index,
            value,
            tt::as_ptr<camel::core::type::StructType>(type));
    } else {
        fromSlot<::Tuple *>(aggregate)->set<slot_t>(index, value, tt::as_ptr<TupleType>(type));
    }
}

/// Walks a primal tree of type `type` together with tangent-shaped trees, rebuilding all of
/// them with `leaf` applied to every trainable leaf.
void walk(Type *type, std::span<const slot_t> in, std::span<slot_t> out, const LeafUpdate &leaf) {
    if (!isAggregate(type)) {
        leaf(type, in, out);
        return;
    }
    Type *tangent = tangentTypeOf(type);
    out[0]        = toSlot<camel::core::rtdata::Object *>(newAggregate(type));
    for (size_t t = 1; t < out.size(); ++t) {
        out[t] = toSlot<camel::core::rtdata::Object *>(newAggregate(tangent));
    }
    std::vector<slot_t> childIn(in.size()), childOut(out.size());
    for (size_t i = 0; i < aggregateSize(type); ++i) {
        Type *elemType      = aggregateElement(type, i);
        const auto position = tangentElementIndex(type, i);
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

bool isTensorLeaf(Type *type) { return camel::tensor::asTensorType(type) != nullptr; }

const TensorObject *floatTensor(slot_t slot) {
    auto *t = fromSlot<TensorObject *>(slot);
    return t->dtype() == TypeCode::Float32 ? t : k::cast(t, TypeCode::Float32, mm::autoSpace());
}

TensorObject *newLike(const TensorObject *t) {
    return TensorObject::create(TypeCode::Float32, t->shapeSpan(), mm::autoSpace());
}

void requireSameShape(const TensorObject *param, const TensorObject *grad, const char *op) {
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
        const TensorObject *p = floatTensor(in[0]);
        const TensorObject *g = floatTensor(in[1]);
        requireSameShape(p, g, "sgd");
        TensorObject *next = newLike(p);
        const float *pp = p->dataAs<float>(), *pg = g->dataAs<float>();
        float *pn       = next->dataAs<float>();
        const auto rate = static_cast<float>(lr);
        for (uint64_t i = 0; i < p->numel(); ++i) {
            pn[i] = pp[i] - rate * pg[i];
        }
        out[0] = toSlot<TensorObject *>(next);
    };
}

struct AdamConfig {
    double lr, beta1, beta2, eps;
    int64_t step; // after this update, starting at 1
};

/// in: param, grad, m, v; out: param, m, v.
LeafUpdate adamLeaf(const AdamConfig &c) {
    const double correct1 = 1.0 - std::pow(c.beta1, static_cast<double>(c.step));
    const double correct2 = 1.0 - std::pow(c.beta2, static_cast<double>(c.step));
    return [c, correct1, correct2](Type *type, std::span<const slot_t> in, std::span<slot_t> out) {
        auto update =
            [&](double p, double g, double m, double v, double &np, double &nm, double &nv) {
                nm = c.beta1 * m + (1.0 - c.beta1) * g;
                nv = c.beta2 * v + (1.0 - c.beta2) * g * g;
                np = p - c.lr * (nm / correct1) / (std::sqrt(nv / correct2) + c.eps);
            };
        if (!isTensorLeaf(type)) {
            double np, nm, nv;
            update(
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
        const TensorObject *p = floatTensor(in[0]);
        const TensorObject *g = floatTensor(in[1]);
        const TensorObject *m = floatTensor(in[2]);
        const TensorObject *v = floatTensor(in[3]);
        requireSameShape(p, g, "adam");
        requireSameShape(p, m, "adam");
        requireSameShape(p, v, "adam");
        TensorObject *np = newLike(p), *nm = newLike(p), *nv = newLike(p);
        const float *pp = p->dataAs<float>(), *pg = g->dataAs<float>();
        const float *pm = m->dataAs<float>(), *pv = v->dataAs<float>();
        float *op = np->dataAs<float>(), *om = nm->dataAs<float>(), *ov = nv->dataAs<float>();
        for (uint64_t i = 0; i < p->numel(); ++i) {
            double a, b, d;
            update(pp[i], pg[i], pm[i], pv[i], a, b, d);
            op[i] = static_cast<float>(a);
            om[i] = static_cast<float>(b);
            ov[i] = static_cast<float>(d);
        }
        out[0] = toSlot<TensorObject *>(np);
        out[1] = toSlot<TensorObject *>(nm);
        out[2] = toSlot<TensorObject *>(nv);
    };
}

/// in: param; out: param (unchanged), zero, zero.
void zerosLeaf(Type *type, std::span<const slot_t> in, std::span<slot_t> out) {
    out[0] = in[0];
    if (!isTensorLeaf(type)) {
        out[1] = out[2] = floatSlot(type, 0.0);
        return;
    }
    const auto *p = fromSlot<TensorObject *>(in[0]);
    for (size_t t = 1; t <= 2; ++t) {
        out[t] = toSlot<TensorObject *>(
            TensorObject::create(TypeCode::Float32, p->shapeSpan(), mm::autoSpace(), true));
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
    Type *tangent = tangentTypeOf(params);
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
        if (!state->treesType()->equals(treesType)) {
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

// ---------------------------------------------------------------- types

bool isNumber(Type *t) {
    return t->code() == TypeCode::Float64 || t->code() == TypeCode::Float32 ||
           t->code() == TypeCode::Int64 || t->code() == TypeCode::Int32;
}

/// Whether `grads` can be the gradient of `params`.
bool gradientOf(Type *params, Type *grads) {
    Type *tangent = tangentTypeOf(params);
    return tangent != nullptr && (tangent->equals(grads) || tangent->assignableFrom(grads));
}

class OptimizerResolver final : public camel::core::type::FuncTypeResolver {
  public:
    enum class Kind { Sgd, AdamState, Adam };
    explicit OptimizerResolver(Kind kind) : kind_(kind) {}

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        if (!with.empty() || norm.empty() || tangentTypeOf(norm[0]) == nullptr) {
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
    };
    return groups;
}

std::unordered_map<std::string, operator_t> optimizerKernels() {
    return {
        {"sgd", &sgdKernel},
        {"adam_state", &adamStateKernel},
        {"adam", &adamKernel},
    };
}

} // namespace camel::nn
