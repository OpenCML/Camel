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
 * Created: Oct. 06, 2024
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/composite.h"
#include "camel/utils/assert.h"

#include <span>
#include <string_view>
#include <vector>

namespace camel::core::type {

// Forward declaration.
class TupleType;
class StructType;

// Layout information: compute once to avoid duplication.
struct TupleTypeLayout {
    size_t totalSize;
    size_t size;
    size_t refCount;
    size_t alignedTypesSize;
    size_t alignedTypeCodesSize;

    struct DataPtrs {
        Type **types;
        TypeCode *typeCodes;
        size_t *refs;
    };
    DataPtrs ptrs(uint8_t *data) const {
        return {
            reinterpret_cast<Type **>(data),
            reinterpret_cast<TypeCode *>(data + alignedTypesSize),
            reinterpret_cast<size_t *>(data + alignedTypesSize + alignedTypeCodesSize),
        };
    }
};

// Factory for constructing TupleType (may use STL).
class TupleTypeFactory {
    friend class TupleType;

  public:
    TupleTypeFactory() = default;
    explicit TupleTypeFactory(const std::vector<Type *> &types) {
        for (auto *type : types) {
            add(type);
        }
    }

    void add(Type *type) { types_.push_back(type); }
    void set(size_t index, Type *type) {
        if (index >= types_.size()) {
            types_.resize(index + 1, nullptr);
        }
        types_[index] = type;
    }

    // Build an immutable TupleType object.
    TupleType *build();

  private:
    std::vector<Type *> types_;
};

// Immutable TupleType: type information is embedded directly in the object.
class TupleType : public CompositeType {
    friend class TupleTypeFactory;

  public:
    // Direct construction is forbidden; use the factory or create methods.
    TupleType(const TupleType &)            = delete;
    TupleType &operator=(const TupleType &) = delete;

    // A tuple type with these element types. Always a new object: a tuple type that a program
    // can name is not interned yet, because `v as T` between two tuple types is accepted only
    // when they are the same object (castSafetyFrom), and sharing equal types would change
    // which programs that rule accepts (docs/design/graph-model, step 2 status).
    static TupleType *create();
    static TupleType *create(const std::vector<Type *> &types);
    static TupleType *create(std::vector<Type *> &&types);

    // Build from a factory.
    static TupleType *fromFactory(TupleTypeFactory &factory);

    /// The layout of a frame or of a static area: the slot types in order. Interned
    /// (camel/core/type/intern.h): the same slot type objects always give the same immutable
    /// TupleType object, so graphs with equal layouts, and every re-encoding of one graph,
    /// share it. A layout is never the static type of a value a program can cast, which is why
    /// it can be shared where create() cannot.
    static TupleType *layout(std::span<Type *const> slotTypes);

    /// The type of a variable cell: the one-slot mutable object that a `var` variable denotes
    /// and that is shared, by pointer, with every `var` parameter and capture bound to it. It
    /// has the layout of a one-element tuple but is a distinct type (printed `var T`). Equal
    /// value types yield the same cell type object.
    static TupleType *cell(Type *valueType);
    bool isCell() const { return cell_; }

    /// The two built-in tagged forms (design principles 2.9; the layout and the reasons for
    /// it are in docs/technical/29_nullable_and_fallible_values.md). Like a cell, each has
    /// the layout of a tuple and is a distinct type; equal payload types yield the same
    /// object.
    ///  - `T?` (nullable): the null slot when null, otherwise a one-slot tuple `(T)`.
    ///  - `T!` (fallible): a two-slot tuple `(T, Error)` whose second slot is the null slot
    ///    unless the operation failed.
    enum class Tag : uint8_t { None, Nullable, Fallible };
    static TupleType *nullable(Type *payloadType);
    static TupleType *fallible(Type *payloadType);
    Tag tag() const { return tag_; }
    bool isTagged() const { return tag_ != Tag::None; }
    bool isNullable() const { return tag_ == Tag::Nullable; }
    bool isFallible() const { return tag_ == Tag::Fallible; }
    /// `T` of a `T?` or `T!`.
    Type *payloadType() const {
        ASSERT(isTagged(), "payloadType() is defined for T? and T! only");
        return typeAt(0);
    }

    size_t size() const { return size_; }
    Type *typeAt(size_t index) const {
        ASSERT(index < size_, "TupleType: index out of range");
        return const_cast<Type *>(typesPtr_[index]);
    }
    TypeCode codeAt(size_t index) const {
        ASSERT(index < size_, "TupleType: index out of range");
        return typeCodesPtr_[index];
    }
    std::span<Type *const> types() const { return std::span<Type *const>(typesPtr_, size_); }
    std::span<const TypeCode> codes() const {
        return std::span<const TypeCode>(typeCodesPtr_, size_);
    }
    // Indices of slots that may hold managed object pointers. Resolved GC-traced
    // types are included so runtime frames can be cleared and traced without
    // walking non-reference slots.
    size_t refCount() const { return refCount_; }
    const size_t *refs() const { return refsPtr(); } // Return a pointer to the internal array.

    TupleType *slice(size_t start, size_t end) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    /// Puts a value into a tagged form (`T -> T?`, `T -> T!`, `Error -> T!`, `U! -> T!` for a
    /// failed value): the CAST node of `v as T?`, of `return v` under a declared `T!`, and
    /// of `throw e`.
    virtual slot_t castSlotFrom(slot_t value, Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;
    /// Widens every element type, so `var` bindings never hold refined aggregates (mutation
    /// through `var` parameters would otherwise break covariant assignability).
    virtual Type *widened() const override;

  private:
    // Fill in one pass from vector<Type*>.
    TupleType(const TupleTypeLayout &layout, const std::vector<Type *> &types);
    // A new, unshared object: what every constructor of this class starts from.
    static TupleType *allocate(const std::vector<Type *> &types);

    bool cell_ = false;      // A variable cell, see cell().
    Tag tag_   = Tag::None;  // A tagged form, see nullable() and fallible().
    size_t size_;            // Element count.
    size_t refCount_;        // Number of reference indices.
    Type **typesPtr_;        // Pointer to the types array (computed at construction).
    TypeCode *typeCodesPtr_; // Pointer to the typeCodes array (computed at construction).
    size_t *refsPtr_;        // Pointer to the refs array (computed at construction).
    // Flexible array: stores types_[size_], typeCodes_[size_], refs_[refCount_].
    // Layout:
    // [TupleType base][size_][refCount_][typesPtr_][typeCodesPtr_][refsPtr_]
    // [types_[size_]][typeCodes_[size_]][refs_[refCount_]]
    uint8_t data_[]; // Flexible array: stores all data.

    // Accessors (return the stored pointers directly).
    const Type **typesPtr() const { return const_cast<const Type **>(typesPtr_); }
    const TypeCode *typeCodesPtr() const { return typeCodesPtr_; }
    const size_t *refsPtr() const { return refsPtr_; }
};

/// The fixed built-in error structure (design principles 2.9): `{ kind: string, message:
/// string, location: string }`. One object; every fallible type carries it.
StructType *errorType();
/// Field indices of errorType(), in its own field order.
size_t errorFieldIndex(std::string_view name);

/// `type` as a tagged tuple type, or nullptr when it is not `T?` / `T!`.
inline TupleType *asTagged(Type *type) {
    if (type && type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        if (tuple->isTagged()) {
            return tuple;
        }
    }
    return nullptr;
}
inline TupleType *asNullable(Type *type) {
    TupleType *tuple = asTagged(type);
    return tuple && tuple->isNullable() ? tuple : nullptr;
}
inline TupleType *asFallible(Type *type) {
    TupleType *tuple = asTagged(type);
    return tuple && tuple->isFallible() ? tuple : nullptr;
}

/// Whether a value of type `source` enters a position of type `target` by itself (design
/// principles 2.10): it is assignable; or `target` is `T?` / `T!` and the value enters `T`
/// (so it is put into the form, one level at a time: an `int` enters `int?!`); or `source`
/// is the type of `null` and `target` is a `T?`; or `source` is the type of the empty array
/// literal `[]` and `target` is an array type. Never the other way: a `T?` is not a `T`.
bool entersImplicitly(Type *target, Type *source);
/// The type two branch arms (or two elements of an array literal) have in common: the type
/// they unify to, or the tagged form one of them enters (`int` and `null` give `int?`, `int`
/// and `int!` give `int!`). nullptr when there is none.
Type *joinTypes(Type *a, Type *b);

/// The type of the value an argument of type `type` carries: the content type of a variable
/// cell, `type` itself otherwise. Overloads are resolved on value types.
inline Type *valueTypeOf(Type *type) {
    if (type && type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        if (tuple->isCell()) {
            return tuple->typeAt(0);
        }
    }
    return type;
}

} // namespace camel::core::type
