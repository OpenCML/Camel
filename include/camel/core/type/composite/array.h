// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type/composite/composite.h"
#include "camel/core/type/extent.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::type {

// Forward declaration.
class ArrayType;

/**
 * How an array's elements are stored (design: docs/design/arrays-memory/README.cn.md 3.1).
 * This is the single source of the decision: the runtime array representation reads it, and no
 * call site chooses a layout by hand.
 *
 * - Compact: a contiguous run of pure data (type/pure.h). Only a pure-data element type may
 *   use it (the leaf rule: a data block never holds a reference, so blocks cannot form cycles
 *   and can be handed out without a copy). A scalar element is one of the fixed-width numeric
 *   types in arrayElementSize(); a struct or tuple element is laid out field by field by
 *   pureDataLayout(). Elements of a struct array have no per-element Struct object.
 * - Slots: one slot per element, able to hold a reference; the representation for every
 *   element type that is not pure data.
 */
enum class ArrayStorage : uint8_t { Slots, Compact };

/// The storage class of an array with `elementType` (`nullptr` means "not known yet": slots).
ArrayStorage arrayStorageFor(const Type *elementType);

/// Bytes per element of a compact numeric scalar element type. Precondition:
/// isCompactElementType. An aggregate element uses pureDataLayout()->size instead.
size_t arrayElementSize(TypeCode code);

/// True for the numeric scalar type codes that compact arrays and DLPack call a number
/// (ints, floats, bool).
bool isCompactElementType(TypeCode code);

/// True for the element type codes a numeric array accepts: the compact numeric scalars and the
/// complex numbers (design principles 5.4).
bool isNumericScalarTypeCode(TypeCode code);

/**
 * Whether two extents constrain a dimension the same way when each stands alone: a constant is
 * the same dimension only as itself, and a non-constant extent asks for nothing at the call
 * (this is the relation `assignableFrom` uses to hold a constant extent to the source's). Type
 * equality is stricter and is decided over the whole shape: ArrayType::sameShape.
 */
bool sameExtentConstraint(const ExtentForm &a, const ExtentForm &b);

/**
 * The shape `forms` with its dimension names replaced by the numbers of a canonical order: the
 * one representative of every shape that differs only in how its names are spelled (`[n, m]` and
 * `[a, b]`, and `[m, n]` too, are the same shape; `[n, n]` is not `[n, m]`, and `[n]` is not
 * `[n + 1]`). Extents are linear forms over names and their equality is decidable (design
 * principles 5.4): two shapes are one type exactly when their canonical forms are equal.
 */
std::vector<ExtentForm> canonicalShape(std::span<const ExtentForm> forms);

// Factory for constructing ArrayType (may use STL containers).
class ArrayTypeFactory {
    friend class ArrayType;

  public:
    ArrayTypeFactory() = default;
    explicit ArrayTypeFactory(Type *elemType) : elemType_(elemType) {}

    void setElemType(Type *elemType) { elemType_ = elemType; }

    // Build an immutable ArrayType object.
    ArrayType *build();

  private:
    Type *elemType_ = nullptr;
};

/**
 * Immutable ArrayType: an element type plus a shape (design principles 5.4).
 *
 * The shape is a prefix of extents and, possibly, an open rank:
 *  - `T[]` and `T[?]` are the same type: one unknown extent, a fixed rank of one;
 *  - `T[3, 4]` is a fixed rank of two with constant extents;
 *  - `T[n, 3]` keeps the named (linear) form of its first extent; the name is part of the
 *    interned object (a signature keeps it) but not of equality, assignability or printing of
 *    a value's shape;
 *  - `T[..]` / `T[..rest]` are open rank: extents() is a prefix, and any rank at least that
 *    long is accepted; the pack name is what a call binds the remaining dimensions to.
 *
 * Invariants: an empty extent list means open rank (`T[..]`); a fixed rank is at least one, and
 * compact storage is empty + no open rank only for the element-denoting result of pack
 * substitution, which is not an ArrayType. A negative constant extent cannot be normalized and
 * is refused at construction (the caller reports it as an invalid type argument).
 */
class ArrayType : public CompositeType {
    friend class ArrayTypeFactory;

  public:
    // Direct construction is forbidden; use the factory or create methods.
    ArrayType(const ArrayType &)            = delete;
    ArrayType &operator=(const ArrayType &) = delete;

    /// `T[]` (= `T[?]`): one unknown extent. Interned: the same element type and shape always
    /// give the same immutable ArrayType object.
    static ArrayType *create(Type *elemType = nullptr);

    /// The same, with constant extents (`T[3, 4]`). An empty list is `T[]`; a negative extent
    /// is refused with std::invalid_argument (write `?` for an unknown extent).
    static ArrayType *create(Type *elemType, std::span<const int64_t> extents);

    /// The same, with extents as forms over dimension names. A non-empty `pack` makes the rank
    /// open after the prefix (`T[3, ..rest]`); an empty list with an empty pack is `T[]`.
    static ArrayType *
    create(Type *elemType, std::span<const ExtentForm> extents, std::string_view pack = {});

    /// An anonymous open-rank array: `T[..]`, or `T[prefix, ..]` when a prefix is given (the
    /// spelling a bare `..` produces).
    static ArrayType *openRank(Type *elemType, std::span<const ExtentForm> prefix = {});

    /// create() with the factory's element type.
    static ArrayType *fromFactory(ArrayTypeFactory &factory);

    /// create(), under the name the other composite types use for it.
    static ArrayType *fromData(Type *elemType);

    Type *elemType() const { return elemType_; }
    TypeCode elemTypeCode() const { return elemTypeCode_; }

    /// The prefix extents, in declaration order. Empty for an open-rank type with no prefix.
    std::span<const ExtentForm> extents() const { return extents_; }
    /// The number of extents stated (the prefix length, not the runtime rank of a value).
    size_t rank() const { return extents_.size(); }
    /// True when the rank is open (`T[..]`, `T[3, ..rest]`).
    bool anyRank() const { return anyRank_; }

    /// This type with another element type, shape and flags unchanged.
    ArrayType *withElementType(Type *elemType) const;

    /// The type of the elements this array gives along its first dimension (design principles
    /// 5.4: `t[i]` takes the i-th along the first dimension, one row when it has more than one).
    /// One dimension fewer: the element itself for a rank-1 array, `T[d1, ...]` for `T[d0, d1,
    /// ...]`, the element type for a nested array (`T[]` of `T[][]`), and the same open-rank
    /// array when the rank is open. `for x in xs` takes these for every array of every rank, and
    /// the core owns the rule (design principles 5.4): the builder states the element port with
    /// it, the loop interface checks the port against it, and the executors take one of these a
    /// round.
    Type *firstDimensionElement() const;

    /// The bracket text of the shape: `[]`, `[3, 4]`, `[?, 3]`, `[3, ..rest]`, `[..]`.
    std::string shapeText() const;
    /// The shape for the mangled name: rank (`any` or a number), constant extents and the linear
    /// forms of the canonical shape; the spelling of dimension names and pack names does not
    /// appear, because it does not take part in equality.
    std::string shapeMangle() const;
    /// Whether this type has the same shape as `other`: the same rank and openness and canonical
    /// shapes that are equal (a linear form is the same form whatever its names are called).
    bool sameShape(const ArrayType &other) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual Type *unify(Type *other) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual slot_t castSlotFrom(slot_t value, Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;
    /// Widens every element type and keeps the shape's rank, forgetting every dimension value
    /// (they are `?`); what `var` bindings hold. A named pack is dropped.
    virtual Type *widened() const override;
    /// A literal beside a numeric array is an element of it (`x * 2.0` with `x: f32[3]`), so it
    /// takes the array's element type (design principles 5.1). An array whose element type is a
    /// name nobody gave yet gives none.
    virtual Type *literalOperandType() const override;

    std::string_view packName() const override;
    bool extentForms(std::vector<ExtentForm> &out) const override;
    bool openRank() const override { return anyRank_; }
    const Type *
    withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const override;

  private:
    // Private constructor: called by the factory or create.
    ArrayType(
        Type *elemType, std::vector<ExtentForm> extents, bool anyRank, const std::string *packName);

    /// Interned construction after normalization and validation.
    static ArrayType *make(
        Type *elemType, std::vector<ExtentForm> extents, bool anyRank, std::string_view pack);

    Type *elemType_;        // Element type pointer (used by the type system).
    TypeCode elemTypeCode_; // Element type code (used at runtime).
    std::vector<ExtentForm> extents_;
    bool anyRank_;
    const std::string *packName_; // interned, nullptr for an anonymous pack or no pack
};

/// The array type of `type`, or nullptr when `type` is not an array. The counterpart of the
/// per-composite `as...Type` casts, for the code that used to ask a module whether a value was
/// its own kind of type (plan section 5.3).
const ArrayType *asArrayType(const Type *type);

/// True for an array whose element is a numeric scalar: the arrays a tensor operator takes. A
/// nested array (`f32[][]`) is an array of arrays, not one numeric array, and is not one of these.
bool isNumericArrayType(const Type *type);

/// True for the type of an array literal written with no element type at all (`[]`): its element
/// type is `Void` — and nobody wrote one, so every position it stands in fixes it (design
/// principles 5.1, 5.2).
bool isUntypedArrayType(const Type *type);

/// True for `Void` itself and for an array whose element type nobody wrote (`Void[]`, `Void[..]`
/// and the further dimensions of one): what a type is left with where an element type is not
/// stated. `Void` is the "no value" marker rather than an element type, so a type variable takes
/// no such type: the empty literal `[]` is one, states no element type, and leaves the position it
/// stands in to state one — and a generic parameter states none (design principles 5.1, 5.2).
bool isElementTypeUnstated(const Type *type);

} // namespace camel::core::type
