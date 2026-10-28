// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/typecall.h"

#include "camel/core/mm.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <vector>

namespace camel::core::type {

TypeCode TypeCallType::typeCode() {
    static TypeCode code = registerOtherType("TypeCall", TypeFlag::Auxiliary);
    return code;
}

namespace {

constexpr std::string_view kTypeFunctions[] = {
    "ElementOf", "PayloadOf", "KeyOf", "ValueOf", "WithElement"};

} // namespace

bool TypeCallType::isTypeFunction(std::string_view name) {
    return std::find(std::begin(kTypeFunctions), std::end(kTypeFunctions), name) !=
           std::end(kTypeFunctions);
}

Type *TypeCallType::evaluate(
    std::string_view name, std::span<Type *const> arguments, std::string &error) {
    if (name == "WithElement") {
        if (arguments.size() != 2 || arguments[0] == nullptr || arguments[1] == nullptr) {
            error = "it takes an array type and an element type";
            return nullptr;
        }
        if (arguments[0]->code() != TypeCode::Array) {
            error = arguments[0]->toString() + " is not an array";
            return nullptr;
        }
        return static_cast<ArrayType *>(arguments[0])->withElementType(arguments[1]);
    }
    if (arguments.size() != 1 || arguments.front() == nullptr) {
        error = "it takes one type";
        return nullptr;
    }
    Type *type = arguments.front();
    if (name == "ElementOf") {
        if (type->code() == TypeCode::Array) {
            return static_cast<ArrayType *>(type)->elemType();
        }
        if (const CollectionType *collection = asCollectionType(type);
            collection != nullptr && !collection->isMap()) {
            return collection->keyType();
        }
        error = type->toString() + " is neither an array nor a set";
        return nullptr;
    }
    if (name == "PayloadOf") {
        if (TupleType *tagged = asTagged(type)) {
            return tagged->payloadType();
        }
        error = type->toString() + " is neither a T? nor a T!";
        return nullptr;
    }
    if (name == "KeyOf" || name == "ValueOf") {
        const CollectionType *collection = asCollectionType(type);
        if (collection == nullptr || !collection->isMap()) {
            error = type->toString() + " is not a map";
            return nullptr;
        }
        return name == "KeyOf" ? collection->keyType() : collection->valueType();
    }
    error = "it is not a type function";
    return nullptr;
}

TypeCallType *TypeCallType::create(std::string_view name, std::span<Type *const> arguments) {
    const std::string *text = TypeTable::instance().internText(name);
    std::vector<uintptr_t> parts;
    parts.reserve(arguments.size() + 1);
    parts.push_back(reinterpret_cast<uintptr_t>(text));
    for (Type *argument : arguments) {
        parts.push_back(typeKeyWord(TypeTable::instance().canonical(argument)));
    }
    return static_cast<TypeCallType *>(
        TypeTable::instance().internOther(typeCode(), parts, [&]() -> Type * {
            Type **params = OtherType::copyParams(arguments);
            void *mem     = mm::permSpace().alloc(sizeof(TypeCallType), alignof(TypeCallType));
            ASSERT(mem != nullptr, "Failed to allocate TypeCallType from permSpace");
            return new (mem) TypeCallType(text, arguments.size(), params);
        }));
}

std::string TypeCallType::toString() const {
    std::string text = *name_ + "<";
    for (size_t i = 0; i < paramCount_; ++i) {
        text += (i > 0 ? ", " : "") + (params_[i] != nullptr ? params_[i]->toString() : "?");
    }
    return text + ">";
}

std::string TypeCallType::mangle() const {
    std::string text = "TC" + std::to_string(name_->size()) + *name_;
    for (size_t i = 0; i < paramCount_; ++i) {
        text += "_" + (params_[i] != nullptr ? params_[i]->mangle() : std::string("X"));
    }
    return text;
}

Type *TypeCallType::clone(bool) const {
    return const_cast<TypeCallType *>(this);
}

bool TypeCallType::equals(Type *type) const {
    return type == this;
}

bool TypeCallType::assignableFrom(Type *sourceType) const {
    return sourceType == this;
}

// A call is not the type of any value: nothing converts to it.
CastSafety TypeCallType::castSafetyFrom(Type *) const {
    return CastSafety::Forbidden;
}

OtherType *TypeCallType::cloneWithParams(std::span<Type *const> params) const {
    return create(*name_, params);
}

// ---- MatchTypeType ----

TypeCode MatchTypeType::typeCode() {
    static TypeCode code = registerOtherType("MatchType", TypeFlag::Auxiliary);
    return code;
}

uint16_t typeShapeBits(const Type *type) {
    const auto bit = [](unsigned shape) { return static_cast<uint16_t>(1u << shape); };
    uint16_t bits  = bit(3); // the wildcard selects every type
    if (type == nullptr) {
        return bits;
    }
    bool structured = false;
    switch (type->code()) {
    case TypeCode::Struct:
        bits |= bit(0);
        structured = true;
        break;
    case TypeCode::Array:
        bits |= bit(6);
        break;
    case TypeCode::Function:
        bits |= bit(4);
        break;
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        if (tuple->isNullable()) {
            bits |= bit(7);
        } else if (tuple->isFallible()) {
            bits |= bit(8);
        } else if (!tuple->isCell()) {
            bits |= bit(1);
            structured = true;
        }
        break;
    }
    default:
        if (const CollectionType *collection = asCollectionType(type)) {
            bits |= bit(collection->isMap() ? 9 : 10);
        }
        break;
    }
    // Every type that is neither a struct nor an ordinary tuple is a leaf.
    return structured ? bits : static_cast<uint16_t>(bits | bit(2));
}

MatchTypeType *MatchTypeType::create(Type *subject, std::span<const MatchTypeArm> arms) {
    std::vector<Type *> params{subject};
    std::vector<uint16_t> shapes;
    std::vector<uint32_t> identityCounts;
    std::vector<uintptr_t> key{typeKeyWord(TypeTable::instance().canonical(subject))};
    for (const MatchTypeArm &arm : arms) {
        shapes.push_back(arm.shapes);
        identityCounts.push_back(static_cast<uint32_t>(arm.identities.size()));
        key.push_back(arm.shapes);
        key.push_back(arm.identities.size());
        for (Type *identity : arm.identities) {
            params.push_back(identity);
            key.push_back(typeKeyWord(TypeTable::instance().canonical(identity)));
        }
        params.push_back(arm.result);
        key.push_back(typeKeyWord(TypeTable::instance().canonical(arm.result)));
    }
    return static_cast<MatchTypeType *>(
        TypeTable::instance().internOther(typeCode(), key, [&]() -> Type * {
            Type **copy = OtherType::copyParams(params);
            void *mem   = mm::permSpace().alloc(sizeof(MatchTypeType), alignof(MatchTypeType));
            ASSERT(mem != nullptr, "Failed to allocate MatchTypeType from permSpace");
            return new (mem) MatchTypeType(shapes, identityCounts, params.size(), copy);
        }));
}

size_t MatchTypeType::firstParamOf(size_t arm) const {
    size_t at = 1;
    for (size_t i = 0; i < arm; ++i) {
        at += identityCounts_[i] + 1;
    }
    return at;
}

std::span<Type *const> MatchTypeType::armIdentities(size_t arm) const {
    return std::span<Type *const>(params_ + firstParamOf(arm), identityCounts_[arm]);
}

Type *MatchTypeType::armResult(size_t arm) const {
    return params_[firstParamOf(arm) + identityCounts_[arm]];
}

std::string MatchTypeType::toString() const {
    std::string text = "match type " + subject()->toString() + " {";
    for (size_t i = 0; i < armCount(); ++i) {
        text += " case ... => " + armResult(i)->toString() + ";";
    }
    return text + " }";
}

std::string MatchTypeType::mangle() const {
    std::string text = "MT" + subject()->mangle();
    for (size_t i = 0; i < armCount(); ++i) {
        text += "_" + std::to_string(shapes_[i]) + "_" + armResult(i)->mangle();
    }
    return text;
}

Type *MatchTypeType::clone(bool) const {
    return const_cast<MatchTypeType *>(this);
}

bool MatchTypeType::equals(Type *type) const {
    return type == this;
}

bool MatchTypeType::assignableFrom(Type *sourceType) const {
    return sourceType == this;
}

CastSafety MatchTypeType::castSafetyFrom(Type *) const {
    return CastSafety::Forbidden;
}

OtherType *MatchTypeType::cloneWithParams(std::span<Type *const> params) const {
    std::vector<MatchTypeArm> arms;
    size_t at = 1;
    for (size_t i = 0; i < armCount(); ++i) {
        MatchTypeArm arm;
        arm.shapes = shapes_[i];
        for (uint32_t k = 0; k < identityCounts_[i]; ++k) {
            arm.identities.push_back(params[at++]);
        }
        arm.result = params[at++];
        arms.push_back(std::move(arm));
    }
    return create(params[0], arms);
}

// ---- TypePackType ----

TypeCode TypePackType::typeCode() {
    static TypeCode code = registerOtherType("TypePack", TypeFlag::Auxiliary);
    return code;
}

TypePackType *TypePackType::create(std::string_view name, Type *body) {
    const std::string *text = TypeTable::instance().internText(name);
    std::vector<uintptr_t> parts;
    parts.reserve(2);
    parts.push_back(reinterpret_cast<uintptr_t>(text));
    parts.push_back(typeKeyWord(TypeTable::instance().canonical(body)));
    Type *const bodyArg = body;
    return static_cast<TypePackType *>(TypeTable::instance().internOther(
        typeCode(), parts, [&]() -> Type * {
            Type **params = OtherType::copyParams(std::span<Type *const>(&bodyArg, 1));
            void *mem = mm::permSpace().alloc(sizeof(TypePackType), alignof(TypePackType));
            ASSERT(mem != nullptr, "Failed to allocate TypePackType from permSpace");
            return new (mem) TypePackType(text, 1, params);
        }));
}

std::string TypePackType::toString() const { return ".." + body()->toString(); }

std::string TypePackType::mangle() const {
    return "PK" + std::to_string(name_->size()) + *name_ + "_" + body()->mangle();
}

Type *TypePackType::clone(bool) const { return const_cast<TypePackType *>(this); }

bool TypePackType::equals(Type *type) const { return type == this; }

bool TypePackType::assignableFrom(Type *sourceType) const { return sourceType == this; }

// An expansion is not the type of any value of its own: it stands for the tuple substitution
// builds from it, so nothing converts to it while it is still a node.
CastSafety TypePackType::castSafetyFrom(Type *) const { return CastSafety::Forbidden; }

OtherType *TypePackType::cloneWithParams(std::span<Type *const> params) const {
    if (params.size() != 1 || params[0] == nullptr) {
        return nullptr;
    }
    return create(*name_, params[0]);
}

bool describeUnevaluatedTypeCall(Type *type, std::string &what, std::string &why) {
    if (type == nullptr) {
        return false;
    }
    const auto concrete = [](Type *t) { return !containsTypeVar(t); };
    if (type->code() == TypeCallType::typeCode()) {
        const auto *call = static_cast<TypeCallType *>(type);
        // An argument that is itself a computation which failed is the cause.
        for (Type *argument : call->paramsSpan()) {
            if (describeUnevaluatedTypeCall(argument, what, why)) {
                return true;
            }
        }
        if (std::all_of(call->paramsSpan().begin(), call->paramsSpan().end(), concrete)) {
            std::string error;
            if (TypeCallType::evaluate(call->name(), call->paramsSpan(), error) == nullptr) {
                what = call->toString();
                why  = error;
                return true;
            }
        }
        return false;
    }
    if (type->code() == MatchTypeType::typeCode()) {
        const auto *match = static_cast<MatchTypeType *>(type);
        if (describeUnevaluatedTypeCall(match->subject(), what, why)) {
            return true;
        }
        if (concrete(match->subject())) {
            what = "match type " + match->subject()->toString();
            why  = "no case selects " + match->subject()->toString();
            return true;
        }
        return false;
    }
    if (type->isOtherType()) {
        for (Type *param : static_cast<OtherType *>(type)->paramsSpan()) {
            if (describeUnevaluatedTypeCall(param, what, why)) {
                return true;
            }
        }
    }
    switch (type->code()) {
    case TypeCode::Array:
        return describeUnevaluatedTypeCall(static_cast<ArrayType *>(type)->elemType(), what, why);
    case TypeCode::Tuple:
        for (Type *member : static_cast<TupleType *>(type)->types()) {
            if (describeUnevaluatedTypeCall(member, what, why)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

bool containsTypeCall(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Array:
        return containsTypeCall(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto types = static_cast<TupleType *>(type)->types();
        return std::any_of(types.begin(), types.end(), containsTypeCall);
    }
    default:
        break;
    }
    if (type->code() == TypeCallType::typeCode() || type->code() == MatchTypeType::typeCode() ||
        type->code() == TypePackType::typeCode()) {
        return true;
    }
    if (type->isOtherType()) {
        const auto params = static_cast<OtherType *>(type)->paramsSpan();
        return std::any_of(params.begin(), params.end(), containsTypeCall);
    }
    return false;
}

} // namespace camel::core::type
