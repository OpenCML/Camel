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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The built-in collection types `Map<K, V>` and `Set<T>` (design principles 2.9; design in
 * docs/technical/31_map_and_set.md).
 *
 * Both are defined by the core and use the OtherType mechanism: every backend, the collector,
 * the passes and the exporters meet them through the path that exists for module types (an
 * opaque GC-traced reference whose object prints, compares, clones and traces itself), while
 * the names are exported by the global built-in module and need no import.
 *
 * A value of either type is an rtdata::Table (camel/core/rtdata/collection.h), ordered by its
 * keys. The type is what tells a map from a set and what says what the slots hold.
 */

#pragma once

#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"

#include <string>

namespace camel::core::type {

/**
 * Why `type` cannot be the key of a map or the element of a set; empty when it can.
 * Design principles 2.10 (key types): allowed are int, i32, bool, string and tuples of allowed
 * types. A type variable is allowed: the check is made again when it has been replaced.
 */
std::string collectionKeyProblem(Type *type);

/// A `Map<K, V>` or `Set<T>` anywhere in `type` whose key type is not allowed, or nullptr.
/// Substituting a type variable can produce one (`Set<T>` with `T = float`).
OtherType *findInvalidCollectionKey(Type *type);

/// Common part of MapType and SetType: the key type and how values of it are ordered.
class CollectionType : public OtherType {
  public:
    /// The type of the keys of a map, of the elements of a set. Null in the prototypes.
    Type *keyType() const { return paramCount_ > 0 ? params_[0] : nullptr; }
    /// The type of the values of a map; null for a set and in the prototypes.
    virtual Type *valueType() const { return nullptr; }
    bool isMap() const { return isMap_; }
    /// False for the prototypes `Map` and `Set`, which only carry the name.
    bool complete() const { return paramCount_ > 0; }
    /// The array type `as` builds a collection from: `(K, V)[]` for a map, `T[]` for a set.
    ArrayType *sourceArrayType() const { return sourceArrayType_; }
    /// `K[]`: the type of `keys(m)` and of `items(s)`.
    ArrayType *keyArrayType() const { return keyArrayType_; }

    /// The arguments are plain types: type variables are accepted among them.
    bool typeArguments() const override { return true; }

    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;
    slot_t castSlotFrom(slot_t value, Type *sourceType) const override;
    bool assignableFrom(Type *sourceType) const override;
    OtherType *
    cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const override;

  protected:
    CollectionType(TypeCode code, bool isMap, std::span<Type *const> params);

    bool isMap_;
    ArrayType *sourceArrayType_ = nullptr;
    ArrayType *keyArrayType_    = nullptr;
};

class MapType final : public CollectionType {
  public:
    static TypeCode typeCode();
    /// `Map<K, V>`. Interned when neither type holds a type variable.
    static MapType *get(Type *keyType, Type *valueType);
    /// What the name `Map` resolves to before its arguments are applied.
    static MapType *prototype();

    Type *valueType() const override { return paramCount_ > 1 ? params_[1] : nullptr; }
    /// The item type `(K, V)`.
    TupleType *itemType() const { return itemType_; }
    /// `V[]`: the type of `values(m)`.
    ArrayType *valueArrayType() const { return valueArrayType_; }
    /// The type of `m[k]`: `V?`, or `V` itself when it is already nullable
    /// (DESIGN-QUESTION(map-nullable-value)).
    Type *lookupType() const { return lookupType_; }
    /// Whether a found value must be put into a `V?` box (false when `V` is nullable).
    bool lookupBoxes() const { return lookupBoxes_; }

    std::string toString() const override;
    std::string mangle() const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    explicit MapType(std::span<Type *const> params);

    TupleType *itemType_       = nullptr;
    ArrayType *valueArrayType_ = nullptr;
    Type *lookupType_          = nullptr;
    bool lookupBoxes_          = false;
};

class SetType final : public CollectionType {
  public:
    static TypeCode typeCode();
    /// `Set<T>`. Interned when the element type holds no type variable.
    static SetType *get(Type *elemType);
    static SetType *prototype();

    std::string toString() const override;
    std::string mangle() const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    explicit SetType(std::span<Type *const> params);
};

/// The MapType / SetType / either of `type`, or nullptr. Prototypes are not returned.
const MapType *asMapType(const Type *type);
const SetType *asSetType(const Type *type);
const CollectionType *asCollectionType(const Type *type);
/// Like asCollectionType, also for the prototypes `Map` and `Set`.
const CollectionType *asCollectionTypeOrPrototype(const Type *type);

} // namespace camel::core::type
