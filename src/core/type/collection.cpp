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
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The built-in collection types: see camel/core/type/collection.h.
 */

#include "camel/core/type/collection.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"

#include <mutex>
#include <unordered_map>

namespace camel::core::type {

// ---- Key types ----

// What may be a key (design principles 2.10). The reasons are part of the diagnostic.
std::string collectionKeyProblem(Type *type) {
    if (type == nullptr) {
        return "it has no type";
    }
    switch (type->code()) {
    case TypeCode::TypeVar:
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Bool:
    case TypeCode::String:
        return "";
    case TypeCode::Float32:
    case TypeCode::Float64:
        return "a floating-point number has no usable equality as a key (NaN is not equal to "
               "itself, and 0.0 equals -0.0)";
    case TypeCode::Array:
        return "an array can be changed in place, so a key could change after it was inserted; "
               "use a tuple";
    case TypeCode::Tuple: {
        auto *tuple = static_cast<TupleType *>(type);
        if (tuple->isTagged()) {
            return "a value that may be null or may have failed has no equality";
        }
        if (tuple->isCell()) {
            return "a variable is not a value";
        }
        for (Type *member : tuple->types()) {
            if (std::string problem = collectionKeyProblem(member); !problem.empty()) {
                return "its element of type '" + member->toString() +
                       "' cannot be a key: " + problem;
            }
        }
        return "";
    }
    case TypeCode::Struct:
        return "a struct is not a key type; use a tuple of its fields";
    case TypeCode::Function:
        return "a function has no equality";
    default:
        break;
    }
    if (asCollectionTypeOrPrototype(type) != nullptr) {
        return "a map or a set is not a key type";
    }
    return "values of this type have no order";
}

OtherType *findInvalidCollectionKey(Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    const auto among = [](std::span<Type *const> types) -> OtherType * {
        for (Type *member : types) {
            if (OtherType *found = findInvalidCollectionKey(member)) {
                return found;
            }
        }
        return nullptr;
    };
    switch (type->code()) {
    case TypeCode::Array:
        return findInvalidCollectionKey(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple:
        return among(static_cast<TupleType *>(type)->types());
    case TypeCode::Struct:
        return among(static_cast<StructType *>(type)->types());
    case TypeCode::Function: {
        auto *func = static_cast<FunctionType *>(type);
        if (OtherType *found = among(func->withTypesSpan())) {
            return found;
        }
        if (OtherType *found = among(func->normTypesSpan())) {
            return found;
        }
        return func->hasExitType() ? findInvalidCollectionKey(func->exitType()) : nullptr;
    }
    default:
        break;
    }
    if (const CollectionType *coll = asCollectionType(type)) {
        if (!collectionKeyProblem(coll->keyType()).empty()) {
            return const_cast<CollectionType *>(coll);
        }
        return among(coll->paramsSpan());
    }
    return nullptr;
}

// ---- CollectionType ----

CollectionType::CollectionType(TypeCode code, bool isMap, std::span<Type *const> params)
    : OtherType(code, params.size(), copyParams(params)), isMap_(isMap) {
    if (!params.empty()) {
        keyArrayType_ = ArrayType::create(params[0]);
    }
}

// A type is immutable and shared.
Type *CollectionType::clone(bool) const { return const_cast<CollectionType *>(this); }

bool CollectionType::equals(Type *type) const {
    if (this == type) {
        return true;
    }
    if (type == nullptr || type->code() != code_) {
        return false;
    }
    const auto *other = static_cast<const CollectionType *>(type);
    if (other->paramCount_ != paramCount_) {
        return false;
    }
    for (size_t i = 0; i < paramCount_; ++i) {
        if (!params_[i]->equals(other->params_[i])) {
            return false;
        }
    }
    return true;
}

bool CollectionType::assignableFrom(Type *sourceType) const { return equals(sourceType); }

CastSafety CollectionType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr || !complete()) {
        return CastSafety::Forbidden;
    }
    if (equals(sourceType)) {
        return CastSafety::Safe;
    }
    // `any as Map<...>` is refused: nothing at run time could check it.
    if (sourceType->code() != TypeCode::Array) {
        return CastSafety::Forbidden;
    }
    Type *elemType = static_cast<ArrayType *>(sourceType)->elemType();
    if (elemType->equals(sourceArrayType_->elemType())) {
        return CastSafety::Safe;
    }
    // The empty literal `[]` has no element type. A variable of that type may have been
    // appended to, so the conversion checks that the array is empty when it runs.
    if (elemType->code() == TypeCode::Void) {
        return CastSafety::Unsafe;
    }
    return CastSafety::Forbidden;
}

slot_t CollectionType::castSlotFrom(slot_t value, Type *sourceType) const {
    if (sourceType == nullptr || sourceType->code() != TypeCode::Array) {
        return value;
    }
    const auto *array = rtdata::fromSlot<Array *>(value);
    if (static_cast<ArrayType *>(sourceType)->elemType()->code() == TypeCode::Void &&
        array != nullptr && array->size() != 0) {
        error::throwRuntimeFault(
            error::RuntimeDiag::InvalidArgument,
            "as " + toString(),
            "an array whose element type is not known converts only when it is empty, and this "
            "one has " +
                std::to_string(array->size()) + " element(s)");
    }
    return rtdata::toSlot(rtdata::table::fromArray(array, this));
}

Type *CollectionType::cloneWithArguments(
    std::span<const TypeArgument> arguments, std::string &error) const {
    const size_t expected = isMap_ ? 2 : 1;
    const char *usage     = isMap_ ? "Map<K, V>" : "Set<T>";
    std::vector<Type *> types;
    for (const TypeArgument &argument : arguments) {
        if (argument.type == nullptr) {
            error = std::string("its arguments are types (") + usage +
                    "), and a constant ('const ...') was given";
            return nullptr;
        }
        types.push_back(argument.type);
    }
    if (types.size() != expected) {
        error = std::string("it is written ") + usage + ", and " + std::to_string(types.size()) +
                " argument(s) were given";
        return nullptr;
    }
    if (std::string problem = collectionKeyProblem(types[0]); !problem.empty()) {
        error = "'" + types[0]->toString() + "' cannot be a key: " + problem;
        return nullptr;
    }
    return cloneWithParams(std::span<Type *const>(types.data(), types.size()));
}

// ---- MapType ----

namespace {

/// One instance per pair of concrete types. A type that still holds a type variable is not
/// interned: a binding occurrence (`typeas K`) and a use (`K`) mangle alike and must stay
/// distinct objects.
template <typename T, typename Make> T *interned(std::span<Type *const> params, Make make) {
    bool generic = false;
    std::string key;
    for (Type *param : params) {
        generic |= containsTypeVar(param);
        key += param->mangle() + "|";
    }
    if (generic) {
        return make();
    }
    static std::mutex mutex;
    static std::unordered_map<std::string, T *> instances;
    std::lock_guard guard(mutex);
    if (auto it = instances.find(key); it != instances.end()) {
        return it->second;
    }
    T *created = make();
    instances.emplace(std::move(key), created);
    return created;
}

} // namespace

TypeCode MapType::typeCode() {
    static TypeCode code = registerOtherType("Map", TypeFlag::GC_Traced);
    return code;
}

MapType::MapType(std::span<Type *const> params) : CollectionType(typeCode(), true, params) {
    if (params.empty()) {
        return;
    }
    Type *valueType  = params[1];
    itemType_        = TupleType::create(std::vector<Type *>{params[0], valueType});
    sourceArrayType_ = ArrayType::create(itemType_);
    valueArrayType_  = ArrayType::create(valueType);
    // DESIGN-QUESTION(map-nullable-value): `m[k]` is `V?`. When `V` is itself `U?` that would
    // be `U??`, which the language rejects; the lookup then has type `V`, and an absent key
    // and a key bound to null both read as null (`has` tells them apart).
    TupleType *tagged = asTagged(valueType);
    lookupBoxes_      = !(tagged != nullptr && tagged->isNullable());
    lookupType_       = lookupBoxes_ ? TupleType::nullable(valueType) : valueType;
}

MapType *MapType::get(Type *keyType, Type *valueType) {
    ASSERT(keyType != nullptr && valueType != nullptr, "Map needs a key and a value type.");
    Type *params[] = {keyType, valueType};
    return interned<MapType>(std::span<Type *const>(params), [&] {
        void *mem = allocTypeStorage<MapType>(TypeStorageKind::Collection);
        return new (mem) MapType(std::span<Type *const>(params));
    });
}

MapType *MapType::prototype() {
    static MapType *instance = [] {
        void *mem = allocTypeStorage<MapType>(TypeStorageKind::Collection);
        return new (mem) MapType(std::span<Type *const>());
    }();
    return instance;
}

std::string MapType::toString() const {
    if (!complete()) {
        return "Map";
    }
    return "Map<" + params_[0]->toString() + ", " + params_[1]->toString() + ">";
}

std::string MapType::mangle() const {
    if (!complete()) {
        return "Dm0";
    }
    return "Dm" + params_[0]->mangle() + params_[1]->mangle();
}

OtherType *MapType::cloneWithParams(std::span<Type *const> params) const {
    ASSERT(params.size() == 2, "Map takes a key type and a value type.");
    return get(params[0], params[1]);
}

// ---- SetType ----

TypeCode SetType::typeCode() {
    static TypeCode code = registerOtherType("Set", TypeFlag::GC_Traced);
    return code;
}

SetType::SetType(std::span<Type *const> params) : CollectionType(typeCode(), false, params) {
    sourceArrayType_ = keyArrayType_;
}

SetType *SetType::get(Type *elemType) {
    ASSERT(elemType != nullptr, "Set needs an element type.");
    Type *params[] = {elemType};
    return interned<SetType>(std::span<Type *const>(params), [&] {
        void *mem = allocTypeStorage<SetType>(TypeStorageKind::Collection);
        return new (mem) SetType(std::span<Type *const>(params));
    });
}

SetType *SetType::prototype() {
    static SetType *instance = [] {
        void *mem = allocTypeStorage<SetType>(TypeStorageKind::Collection);
        return new (mem) SetType(std::span<Type *const>());
    }();
    return instance;
}

std::string SetType::toString() const {
    return complete() ? "Set<" + params_[0]->toString() + ">" : "Set";
}

std::string SetType::mangle() const { return complete() ? "Ds" + params_[0]->mangle() : "Ds0"; }

OtherType *SetType::cloneWithParams(std::span<Type *const> params) const {
    ASSERT(params.size() == 1, "Set takes an element type.");
    return get(params[0]);
}

// ---- Queries ----

const CollectionType *asCollectionTypeOrPrototype(const Type *type) {
    if (type == nullptr || !type->isOtherType()) {
        return nullptr;
    }
    if (type->code() == MapType::typeCode() || type->code() == SetType::typeCode()) {
        return static_cast<const CollectionType *>(type);
    }
    return nullptr;
}

const CollectionType *asCollectionType(const Type *type) {
    const CollectionType *result = asCollectionTypeOrPrototype(type);
    return result != nullptr && result->complete() ? result : nullptr;
}

const MapType *asMapType(const Type *type) {
    const CollectionType *result = asCollectionType(type);
    return result != nullptr && result->isMap() ? static_cast<const MapType *>(result) : nullptr;
}

const SetType *asSetType(const Type *type) {
    const CollectionType *result = asCollectionType(type);
    return result != nullptr && !result->isMap() ? static_cast<const SetType *>(result) : nullptr;
}

} // namespace camel::core::type
