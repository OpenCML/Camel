// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tangent-space query (see tangent_space.h). It is the same three-tier answer for every type,
 * and it reads the derivative registry for the types a module owns so that a module's space (a
 * tensor's, which is the tensor itself) answers without the core knowing the module.
 */

#include "camel/core/tangent_space.h"

#include "camel/core/derivative.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

#include <format>
#include <vector>

namespace camel::core {

namespace {

using type::ArrayType;
using type::StructType;
using type::StructTypeFactory;
using type::TupleType;
using type::Type;
using type::TypeCode;

TangentSpaceAnswer answerOf(const Type *primal);

/// The product of the members' answers: a tuple keeps the elements' order, a struct keeps its
/// field names. Only called when every member has a space and at least one of them is `Other`.
Type *productOf(const Type *aggregate, std::vector<TangentSpaceAnswer> &members) {
    if (aggregate->code() == TypeCode::Struct) {
        StructTypeFactory factory;
        const auto *structure = tt::as_ptr<StructType>(const_cast<Type *>(aggregate));
        for (size_t i = 0; i < members.size(); ++i) {
            factory.add(std::string(structure->fieldName(i)), members[i].space);
        }
        return static_cast<Type *>(factory.build());
    }
    std::vector<Type *> elements;
    elements.reserve(members.size());
    for (const TangentSpaceAnswer &member : members) {
        elements.push_back(member.space);
    }
    return static_cast<Type *>(TupleType::create(std::move(elements)));
}

/// The answer for an aggregate: the product of its members. One member without a space leaves the
/// aggregate with none, because a tangent of the whole is a tangent of every part.
TangentSpaceAnswer aggregateAnswer(const Type *aggregate) {
    const size_t count = aggregateSize(const_cast<Type *>(aggregate));
    std::vector<TangentSpaceAnswer> members;
    members.reserve(count);
    bool allSelf = true;
    for (size_t i = 0; i < count; ++i) {
        members.push_back(answerOf(aggregateElement(const_cast<Type *>(aggregate), i)));
        if (!members.back().hasSpace()) {
            return {};
        }
        allSelf = allSelf && members.back().kind == TangentSpaceKind::Self;
    }
    if (count == 0) {
        return {};
    }
    if (allSelf) {
        return {TangentSpaceKind::Self, const_cast<Type *>(aggregate)};
    }
    return {TangentSpaceKind::Other, productOf(aggregate, members)};
}

TangentSpaceAnswer answerOf(const Type *primal) {
    if (primal == nullptr) {
        return {};
    }
    switch (primal->code()) {
    case TypeCode::Array: {
        // The element decides: an array of values with a tangent is an array of their tangents,
        // and its shape is the value's own shape (a tensor's tangent is the tensor itself).
        const auto *array = tt::as_ptr<ArrayType>(const_cast<Type *>(primal));
        const Type *element = array->elemType();
        const TangentSpaceAnswer elementAnswer = answerOf(element);
        if (!elementAnswer.hasSpace()) {
            return {};
        }
        if (elementAnswer.kind == TangentSpaceKind::Self) {
            return {TangentSpaceKind::Self, const_cast<Type *>(primal)};
        }
        return {
            TangentSpaceKind::Other, static_cast<Type *>(array->withElementType(elementAnswer.space))};
    }
    case TypeCode::Tuple:
    case TypeCode::Struct:
        return aggregateAnswer(primal);
    default:
        break;
    }
    if (type::isFloatingScalar(primal->code())) {
        // `float`, `f32`, and the two reduced-precision floats: a narrow float is a float, so its
        // tangent is a value of the same type (derivative.cpp registers the same two that way).
        return {TangentSpaceKind::Self, const_cast<Type *>(primal)};
    }
    if (DerivativeRegistry::instance().isDiscrete(primal)) {
        // An integer or a boolean: the derivative is zero by definition and there is nothing to
        // carry, so the value has no tangent space at all (design principles 9.9).
        return {};
    }
    if (const TangentSpace *registered = DerivativeRegistry::instance().findTangentSpace(primal)) {
        // A module owns the type and says what its tangent is: the same type (`tangentType` null,
        // or answering with the primal) or another one.
        Type *tangent = registered->tangentType == nullptr
                            ? nullptr
                            : registered->tangentType(const_cast<Type *>(primal));
        if (tangent == nullptr || tangent == primal) {
            return {TangentSpaceKind::Self, const_cast<Type *>(primal)};
        }
        return {TangentSpaceKind::Other, tangent};
    }
    return {};
}

} // namespace

std::string TangentSpaceAnswer::describe() const {
    switch (kind) {
    case TangentSpaceKind::None:
        return "none";
    case TangentSpaceKind::Self:
        return "self";
    case TangentSpaceKind::Other:
        return space == nullptr ? std::string("other") : std::format("other={}", space->toString());
    }
    return "none";
}

TangentSpaceAnswer tangentSpaceOf(const type::Type *type) { return answerOf(type); }

} // namespace camel::core