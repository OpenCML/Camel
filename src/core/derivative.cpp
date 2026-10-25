// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Derivative registry (see derivative.h).
 */

#include "camel/core/derivative.h"

#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::core {

vjp_node_t VjpBuilder::addStaticFloat(double value) {
    return addStatic(rtdata::toSlot<rtdata::Float64>(value), type::Type::Float64());
}

vjp_node_t VjpBuilder::addAccess(vjp_node_t aggregate, size_t index) {
    (void)aggregate;
    (void)index;
    throw std::logic_error("VjpBuilder::addAccess is not implemented by this builder");
}

void noGradient(VjpBuilder &builder, const VjpCall &call) {
    (void)builder;
    (void)call;
}

DerivativeRegistry &DerivativeRegistry::instance() {
    static DerivativeRegistry registry;
    return registry;
}

void DerivativeRegistry::setRule(std::string_view uri, VjpRule rule) {
    std::unique_lock lock(mutex_);
    rules_.insert_or_assign(std::string(uri), rule);
}

VjpRule DerivativeRegistry::findRule(std::string_view uri) const {
    std::shared_lock lock(mutex_);
    auto it = rules_.find(uri);
    return it == rules_.end() ? nullptr : it->second;
}

void DerivativeRegistry::setTangentSpace(type::TypeCode code, TangentSpace space) {
    std::unique_lock lock(mutex_);
    auto &spaces = tangents_[code];
    if (space.owns == nullptr) {
        // A space that owns every value of the code replaces the code's space: nothing else has
        // to answer for it. A subset space is kept, and still answers for the values it owns.
        spaces.clear();
        spaces.push_back(std::move(space));
        return;
    }
    for (TangentSpace &existing : spaces) {
        if (existing.owns == space.owns) {
            existing = std::move(space);
            return;
        }
    }
    spaces.push_back(std::move(space));
}

const TangentSpace *DerivativeRegistry::findTangentSpace(const type::Type *type) const {
    if (type == nullptr) {
        return nullptr;
    }
    std::shared_lock lock(mutex_);
    auto it = tangents_.find(type->code());
    if (it == tangents_.end()) {
        return nullptr;
    }
    // The last space that owns the value: a module that claimed a subset of a shared code is
    // registered after the code's own space, and its values are the ones it answers for.
    for (auto space = it->second.rbegin(); space != it->second.rend(); ++space) {
        if (space->owns == nullptr || space->owns(type)) {
            return &*space;
        }
    }
    return nullptr;
}

void DerivativeRegistry::setDiscretePredicate(
    type::TypeCode code, bool (*predicate)(const type::Type *)) {
    std::unique_lock lock(mutex_);
    discrete_.insert_or_assign(code, predicate);
}

bool DerivativeRegistry::isDiscrete(const type::Type *type) const {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case type::TypeCode::Int32:
    case type::TypeCode::Int64:
    case type::TypeCode::Bool:
        return true;
    default:
        break;
    }
    std::shared_lock lock(mutex_);
    auto it = discrete_.find(type->code());
    return it != discrete_.end() && it->second(type);
}

// ---------------------------------------------------------------- tangent types

using type::ArrayType;
using type::StructType;
using type::StructTypeFactory;
using type::TupleType;
using type::Type;
using type::TypeCode;

namespace {

/// The element at `index` of an aggregate: a struct's field, a tuple's element.
Type *elementAt(Type *aggregate, size_t index) {
    return aggregate->code() == TypeCode::Struct
               ? tt::as_ptr<StructType>(aggregate)->typeAt(index)
               : tt::as_ptr<TupleType>(aggregate)->typeAt(index);
}

/// Whether the tangent of `aggregate` keeps element `index`. Every element that has a tangent is
/// kept, at that tangent type. A struct also keeps its discrete elements -- an integer, a
/// boolean, or a tensor of them -- at the element's *own* type (the author's ruling of
/// 2026-10-25: `with_grad(loss)<m>(x, y)` gives a gradient "of the same type as `m`", so
/// `labels: i64[..]` is a field of the gradient too and its value is the zero of that shape and
/// element type). An optimizer pairs the parameter tree and the gradient tree structurally and
/// needs no special case for a discrete field. A tuple keeps only the elements with a tangent:
/// it is the machinery's own aggregate (a loop's state, a call's ports), not a model, and the
/// ruling is about the gradient of a model.
bool keepsInTangent(Type *aggregate, size_t index) {
    Type *elem = elementAt(aggregate, index);
    if (tangentTypeOf(elem) != nullptr) {
        return true;
    }
    return aggregate->code() == TypeCode::Struct &&
           DerivativeRegistry::instance().isDiscrete(elem);
}

Type *computeTangentType(Type *primal) {
    switch (primal->code()) {
    case TypeCode::Struct: {
        auto *st = tt::as_ptr<StructType>(primal);
        StructTypeFactory factory;
        bool any = false;
        for (size_t i = 0; i < st->size(); ++i) {
            if (!keepsInTangent(primal, i)) {
                continue;
            }
            Type *field = st->typeAt(i);
            Type *elem  = tangentTypeOf(field);
            factory.add(std::string(st->fieldName(i)), elem != nullptr ? elem : field);
            any = any || elem != nullptr;
        }
        // A struct none of whose fields has a tangent is not differentiable at all: a gradient of
        // it would be a struct of zeros, and `grad` refuses such a parameter (test/cases/modules/
        // autodiff/grad_refuse_struct.cml, `Plain = {s: string, n: int}`) instead of handing back
        // a gradient the program cannot have meant. The ruling of 2026-10-25 is about the
        // gradient of a model that has one: its discrete fields are in the gradient too, at their
        // own type, so the tangent tree pairs with the parameter tree.
        return any ? factory.build() : nullptr;
    }
    case TypeCode::Tuple: {
        auto *tuple = tt::as_ptr<TupleType>(primal);
        std::vector<Type *> elems;
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (keepsInTangent(primal, i)) {
                elems.push_back(tangentTypeOf(tuple->typeAt(i)));
            }
        }
        return elems.empty() ? nullptr : TupleType::create(std::move(elems));
    }
    case TypeCode::Array: {
        const ArrayType *array = tt::as_ptr<ArrayType>(primal);
        // A numeric array is a tensor, and a tensor's tangent is a tensor of the same shape:
        // building it from the element's tangent -- the array of the elements' tangents -- leaves
        // out the extents of the value's own outermost dimension. `f32[2, 3]` came back as
        // `f32[]` that way, and that is what a loop over the array scattered its rounds' row
        // gradients into: the zero tangent and the scatter were typed one rank short of the array
        // they accumulate into, and the gradient was two uninitialized floats where the hand
        // adjoint is six ones
        // (test/cases/modules/autodiff/loop_grad_refuse_slice_iterable.cml, `shape(d)` was `[2]`
        // while the value is `[2, 3]`). The construction still answers for an array whose elements
        // are arrays *as values* -- a nested array, whose elements' tangents are arrays of their
        // elements' tangents -- and such an array is not a numeric array. Whether the elements have
        // a tangent at all is still the element's answer: an array of integers has none, and it
        // must not be handed one just because its element type is numeric (a discrete loop's state).
        if (isNumericArrayType(primal) && array->elemType() != nullptr &&
            tangentTypeOf(array->elemType()) != nullptr) {
            return primal;
        }
        Type *elem    = array->elemType();
        Type *tangent = elem ? tangentTypeOf(elem) : nullptr;
        if (tangent == nullptr) {
            // An array whose element has no tangent has no tangent of its own: its coproduct is
            // the elements' coproducts, and they are nothing.
            return nullptr;
        }
        // The tangent of an array is an array of the same shape: a tangent space has the shape of
        // the space it differentiates, and `gradientOf` compares the gradient of a parameter with
        // the parameter's own tangent type. An array of a tangent scalar that forgot the shape
        // (`ArrayType::create(tangent)`, one unknown extent) is the tangent of nothing but a rank-1
        // value, and made `nn::sgd` refuse a rank-2 parameter's gradient as a different type. The
        // element type of the value is always in its type, so the element's tangent is the only
        // thing to take: a kind that would state a tangent of its own states a whole tangent
        // *space* instead, and that is what the lookup below answers.
        return tangent == elem ? const_cast<ArrayType *>(array) : array->withElementType(tangent);
    }
    default:
        break;
    }
    const auto *space = DerivativeRegistry::instance().findTangentSpace(primal);
    if (space == nullptr) {
        return nullptr;
    }
    return space->tangentType ? space->tangentType(primal) : primal;
}

} // namespace

Type *tangentTypeOf(Type *primal) {
    if (primal == nullptr) {
        return nullptr;
    }
    // Types are immutable and long-lived, so the mapping is cached by identity.
    static std::mutex mutex;
    static std::unordered_map<Type *, Type *> cache;
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(primal); it != cache.end()) {
            return it->second;
        }
    }
    Type *tangent = computeTangentType(primal);
    std::lock_guard lock(mutex);
    cache.emplace(primal, tangent);
    return tangent;
}

bool isAggregate(const Type *t) {
    return t != nullptr && (t->code() == TypeCode::Struct || t->code() == TypeCode::Tuple);
}

size_t aggregateSize(Type *aggregate) {
    return aggregate->code() == TypeCode::Struct ? tt::as_ptr<StructType>(aggregate)->size()
                                                 : tt::as_ptr<TupleType>(aggregate)->size();
}

Type *aggregateElement(Type *aggregate, size_t index) { return elementAt(aggregate, index); }

std::optional<size_t> tangentElementIndex(Type *aggregate, size_t index) {
    if (!keepsInTangent(aggregate, index)) {
        return std::nullopt;
    }
    size_t position = 0;
    for (size_t i = 0; i < index; ++i) {
        if (keepsInTangent(aggregate, i)) {
            ++position;
        }
    }
    return position;
}

} // namespace camel::core
