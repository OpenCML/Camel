// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/typecall.h"

#include "camel/core/mm.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <vector>

namespace camel::core::type {

TypeCode TypeCallType::typeCode() {
    static TypeCode code = registerOtherType("TypeCall", TypeFlag::Auxiliary);
    return code;
}

namespace {

constexpr std::string_view kTypeFunctions[] = {"element_of", "payload_of", "key_of", "value_of"};

} // namespace

bool TypeCallType::isTypeFunction(std::string_view name) {
    return std::find(std::begin(kTypeFunctions), std::end(kTypeFunctions), name) !=
           std::end(kTypeFunctions);
}

Type *TypeCallType::evaluate(
    std::string_view name, std::span<Type *const> arguments, std::string &error) {
    if (arguments.size() != 1 || arguments.front() == nullptr) {
        error = "it takes one type";
        return nullptr;
    }
    Type *type = arguments.front();
    if (name == "element_of") {
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
    if (name == "payload_of") {
        if (TupleType *tagged = asTagged(type)) {
            return tagged->payloadType();
        }
        error = type->toString() + " is neither a T? nor a T!";
        return nullptr;
    }
    if (name == "key_of" || name == "value_of") {
        const CollectionType *collection = asCollectionType(type);
        if (collection == nullptr || !collection->isMap()) {
            error = type->toString() + " is not a map";
            return nullptr;
        }
        return name == "key_of" ? collection->keyType() : collection->valueType();
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
    std::string text = *name_ + "::<";
    for (size_t i = 0; i < paramCount_; ++i) {
        text += (i > 0 ? ", " : "") + (params_[i] != nullptr ? params_[i]->toString() : "?");
    }
    return text + ">()";
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
    if (type->code() == TypeCallType::typeCode()) {
        return true;
    }
    if (type->isOtherType()) {
        const auto params = static_cast<OtherType *>(type)->paramsSpan();
        return std::any_of(params.begin(), params.end(), containsTypeCall);
    }
    return false;
}

} // namespace camel::core::type
