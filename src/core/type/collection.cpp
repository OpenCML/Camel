// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The built-in collection types: see camel/core/type/collection.h.
 */

#include "camel/core/type/collection.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace camel::core::type {

// ---- Key types ----

// What may be a key (design principles 11.3). The reasons are part of the diagnostic.
std::string collectionKeyProblem(
    Type *type, camel::source::origin_id_t *fieldOrigin, std::string *fieldLabel) {
    if (type == nullptr) {
        return "it has no type";
    }
    if (const EnumType *enumType = asEnumType(type); enumType != nullptr) {
        // An enum can be a key when every field of every variant is one (design principles
        // 11.3): its order is the variants' declaration order, then field by field.
        for (const EnumVariant &variant : enumType->variants()) {
            for (size_t i = 0; i < variant.fieldTypes.size(); ++i) {
                Type *field         = variant.fieldTypes[i];
                std::string problem = collectionKeyProblem(field);
                if (problem.empty()) {
                    continue;
                }
                const std::string where =
                    variant.shape == VariantShape::Struct && i < variant.fieldNames.size()
                        ? "field '" + variant.fieldNames[i] + "' of variant '" + variant.name + "'"
                        : "field " + std::to_string(i) + " of variant '" + variant.name + "'";
                if (fieldOrigin != nullptr && i < variant.fieldOrigins.size()) {
                    *fieldOrigin = variant.fieldOrigins[i];
                }
                if (fieldLabel != nullptr) {
                    *fieldLabel = where;
                }
                return "its " + where + " of type '" + field->toString() +
                       "' cannot be a key: " + problem;
            }
        }
        return "";
    }
    switch (type->code()) {
    case TypeCode::TypeVar:
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Bool:
    case TypeCode::String:
        return "";
    case TypeCode::EnumHole:
        return "its type is not determined here, so it cannot be a key";
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
            if (std::string problem = collectionKeyProblem(member, fieldOrigin, fieldLabel);
                !problem.empty()) {
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

namespace {

OtherType *findInvalidCollectionKeyImpl(Type *type, std::vector<Type *> &path) {
    if (type == nullptr) {
        return nullptr;
    }
    // A named struct may mention itself through an array; its fields would otherwise be walked
    // forever. Seeing the same type object on the current path means its parts have already
    // been searched.
    if (std::find(path.begin(), path.end(), type) != path.end()) {
        return nullptr;
    }
    const TypeCode code = type->code();
    const bool composite = code == TypeCode::Array || code == TypeCode::Tuple ||
                           code == TypeCode::Struct || code == TypeCode::Function ||
                           asCollectionType(type) != nullptr;
    if (!composite) {
        return nullptr;
    }
    path.push_back(type);
    OtherType *found  = nullptr;
    const auto among = [&path](std::span<Type *const> types) -> OtherType * {
        for (Type *member : types) {
            if (OtherType *invalid = findInvalidCollectionKeyImpl(member, path)) {
                return invalid;
            }
        }
        return nullptr;
    };
    switch (code) {
    case TypeCode::Array:
        found = findInvalidCollectionKeyImpl(static_cast<ArrayType *>(type)->elemType(), path);
        break;
    case TypeCode::Tuple:
        found = among(static_cast<TupleType *>(type)->types());
        break;
    case TypeCode::Struct:
        found = among(static_cast<StructType *>(type)->types());
        break;
    case TypeCode::Function: {
        auto *func = static_cast<FunctionType *>(type);
        found      = among(func->withTypesSpan());
        if (found == nullptr) {
            found = among(func->normTypesSpan());
        }
        if (found == nullptr && func->hasExitType()) {
            found = findInvalidCollectionKeyImpl(func->exitType(), path);
        }
        break;
    }
    default:
        break;
    }
    if (found == nullptr) {
        if (const CollectionType *coll = asCollectionType(type)) {
            if (!collectionKeyProblem(coll->keyType()).empty()) {
                found = const_cast<CollectionType *>(coll);
            } else {
                found = among(coll->paramsSpan());
            }
        }
    }
    path.pop_back();
    return found;
}

} // namespace

OtherType *findInvalidCollectionKey(Type *type) {
    std::vector<Type *> path;
    return findInvalidCollectionKeyImpl(type, path);
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

bool CollectionType::equals(Type *type) const { return typeEquals(this, type); }

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
    if (isUntypedArrayType(sourceType) && array != nullptr && array->size() != 0) {
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

/// One entry per kind and canonical type arguments, from the type table (see intern.h): a
/// `Map<K, V>` compares its arguments with `equals` and its kind is its name, so the arguments
/// enter the key by canonical id, exactly as a named struct's do. A type that still holds a type
/// variable is one entry like any other: the variable is interned itself, so the declaration's
/// parameter and a use of it stay the types they are.
template <typename T, typename Make> T *interned(TypeCode code, std::span<Type *const> params, Make make) {
    std::vector<uintptr_t> key;
    key.reserve(1 + params.size());
    key.push_back(static_cast<uintptr_t>(code));
    for (Type *param : params) {
        key.push_back(typeKeyWord(TypeTable::instance().canonical(param)));
    }
    return static_cast<T *>(
        TypeTable::instance().intern(TypeTable::Shape::Collection, key, [&]() -> Type * {
            return make();
        }));
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
    return interned<MapType>(typeCode(), std::span<Type *const>(params), [&] {
        void *mem = allocTypeStorage<MapType>(TypeStorageKind::Collection);
        return new (mem) MapType(std::span<Type *const>(params));
    });
}

MapType *MapType::prototype() {
    // The prototype is one type too: no arguments, one id (its own canonical form).
    return interned<MapType>(typeCode(), std::span<Type *const>(), [&] {
        void *mem = allocTypeStorage<MapType>(TypeStorageKind::Collection);
        return new (mem) MapType(std::span<Type *const>());
    });
}

std::string MapType::toString() const { return typeToString(this); }

std::string MapType::mangle() const { return typeMangle(this); }

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
    return interned<SetType>(typeCode(), std::span<Type *const>(params), [&] {
        void *mem = allocTypeStorage<SetType>(TypeStorageKind::Collection);
        return new (mem) SetType(std::span<Type *const>(params));
    });
}

SetType *SetType::prototype() {
    return interned<SetType>(typeCode(), std::span<Type *const>(), [&] {
        void *mem = allocTypeStorage<SetType>(TypeStorageKind::Collection);
        return new (mem) SetType(std::span<Type *const>());
    });
}

std::string SetType::toString() const { return typeToString(this); }

std::string SetType::mangle() const { return typeMangle(this); }

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
