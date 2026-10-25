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

/// Who the answer is for. This -- and not the aggregate's shape -- is what the two questions
/// differ by (the author's ruling of 2026-10-25 on
/// discrete-tuple-tangent-space-and-machinery-aggregates: "the distinction is who it is for, not
/// struct or tuple"). `User` is the gradient of a parameter the user differentiates;
/// `Machinery` is a value the transform carries itself, which the user never sees.
enum class Audience { User, Machinery };

Type *computeTangent(Type *primal, Audience audience);

/// The element at `index` of an aggregate: a struct's field, a tuple's element.
Type *elementAt(Type *aggregate, size_t index) {
    return aggregate->code() == TypeCode::Struct
               ? tt::as_ptr<StructType>(aggregate)->typeAt(index)
               : tt::as_ptr<TupleType>(aggregate)->typeAt(index);
}

/// Whether the tangent the *machinery* carries for `aggregate` keeps element `index`: the
/// machinery's own question, "does this value need a gradient", not the language's. A member
/// whose gradient is zero whatever the program does (a discrete one) needs no channel, and
/// neither does a function or any other member with no tangent of its own. A struct and a tuple
/// answer by the same rule -- they are the same kind of aggregate to the transform -- and a
/// member's shape is not what decides.
bool keepsInTangent(Type *aggregate, size_t index) {
    return computeTangent(elementAt(aggregate, index), Audience::Machinery) != nullptr;
}

Type *computeTangent(Type *primal, Audience audience) {
    switch (primal->code()) {
    case TypeCode::Struct:
    case TypeCode::Tuple: {
        // One rule for both shapes. What decides how a member is treated is the audience: the
        // user's gradient keeps every member in its place, the machinery keeps only the members
        // that need a gradient.
        const bool isStruct = primal->code() == TypeCode::Struct;
        StructTypeFactory factory;
        std::vector<Type *> elements;
        size_t kept = 0;
        for (size_t i = 0; i < aggregateSize(primal); ++i) {
            Type *member = elementAt(primal, i);
            Type *answer = computeTangent(member, audience);
            if (answer == nullptr) {
                if (audience == Audience::User) {
                    // A gradient is taken member by member, so one member without a gradient
                    // (`s: string`, a function) makes the aggregate itself have none.
                    return nullptr;
                }
                // The machinery drops it: the member carries no gradient, so nothing is lost.
                continue;
            }
            if (isStruct) {
                factory.add(
                    std::string(tt::as_ptr<StructType>(primal)->fieldName(i)), answer);
            } else {
                elements.push_back(answer);
            }
            ++kept;
        }
        if (kept == 0) {
            // An aggregate with no member left carries nothing. (A struct with no field at all,
            // and a machinery aggregate whose every member is discrete, are both this.)
            return nullptr;
        }
        return isStruct ? static_cast<Type *>(factory.build())
                        : static_cast<Type *>(TupleType::create(std::move(elements)));
    }
    case TypeCode::Array: {
        const ArrayType *array = tt::as_ptr<ArrayType>(primal);
        Type *elem             = array->elemType();
        Type *elementAnswer    = elem ? computeTangent(elem, audience) : nullptr;
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
        // an answer at all is still the element's answer, for the same audience: an array of
        // strings has none, and an array of integers has none *to the machinery* -- it must not be
        // handed one just because its element type is numeric (a discrete loop's state).
        if (isNumericArrayType(primal) && elementAnswer != nullptr) {
            return primal;
        }
        if (elementAnswer == nullptr) {
            // An array whose element has no answer has none of its own: its coproduct is
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
        return elementAnswer == elem ? const_cast<ArrayType *>(array)
                                     : array->withElementType(elementAnswer);
    }
    default:
        break;
    }
    const auto *space = DerivativeRegistry::instance().findTangentSpace(primal);
    if (space != nullptr) {
        return space->tangentType ? space->tangentType(primal) : primal;
    }
    if (audience == Audience::User && DerivativeRegistry::instance().isDiscrete(primal)) {
        // A number and a boolean are their own gradient, with the value zero: the gradient of such
        // a parameter is the parameter's own type (`with <n: int>` gets an `int` back, and a
        // discrete field of a model is kept in its place). The machinery drops these members
        // instead -- their gradient is zero whatever the program does -- so it does not ask this.
        return primal;
    }
    return nullptr;
}

} // namespace

namespace {

/// The answer of `computeTangent` for one audience, cached. Types are immutable and long-lived, so
/// the mapping is cached by identity; the two audiences keep separate tables, since one type
/// answers two different questions.
Type *cachedTangent(Type *primal, Audience audience) {
    if (primal == nullptr) {
        return nullptr;
    }
    static std::mutex mutex;
    static std::unordered_map<Type *, Type *> forUser;
    static std::unordered_map<Type *, Type *> forMachinery;
    auto &cache = audience == Audience::User ? forUser : forMachinery;
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(primal); it != cache.end()) {
            return it->second;
        }
    }
    Type *tangent = computeTangent(primal, audience);
    std::lock_guard lock(mutex);
    cache.emplace(primal, tangent);
    return tangent;
}

} // namespace

Type *gradientTypeOf(Type *parameter) {
    return cachedTangent(parameter, Audience::User);
}

Type *machineryTangentTypeOf(Type *primal) {
    return cachedTangent(primal, Audience::Machinery);
}

bool isAggregate(const Type *t) {
    return t != nullptr && (t->code() == TypeCode::Struct || t->code() == TypeCode::Tuple);
}

size_t aggregateSize(Type *aggregate) {
    return aggregate->code() == TypeCode::Struct ? tt::as_ptr<StructType>(aggregate)->size()
                                                 : tt::as_ptr<TupleType>(aggregate)->size();
}

Type *aggregateElement(Type *aggregate, size_t index) { return elementAt(aggregate, index); }

std::optional<size_t> gradientElementIndex(Type *aggregate, size_t index) {
    // Every member of an aggregate that has a gradient has one, and stands in its own place: the
    // grading is the same shape as the aggregate being graded. An aggregate with no gradient has
    // no positions at all.
    if (gradientTypeOf(aggregate) == nullptr || index >= aggregateSize(aggregate)) {
        return std::nullopt;
    }
    return index;
}

std::optional<size_t> machineryTangentElementIndex(Type *aggregate, size_t index) {
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
