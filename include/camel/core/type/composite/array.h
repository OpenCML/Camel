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

/// True for the element type codes the numeric-unknown array (`Tensor`) accepts: the compact
/// numeric scalars and the complex numbers (design principles 5.4, plan section 5.1).
bool isNumericScalarTypeCode(TypeCode code);

/**
 * Whether two extents constrain a dimension the same way (type equality; names are not part of
 * it). A constant is the same dimension only as itself; every non-constant extent (unknown, or
 * a linear form over signature names) means "any size" for every check, which is also how the
 * tensor type's shape comparison worked before this type carried dimensions.
 */
bool sameExtentConstraint(const ExtentForm &a, const ExtentForm &b);

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
 *
 * `numericUnknown()` marks the numeric-unknown array (`Tensor`): the element type is not fixed
 * yet but is known to be a numeric scalar, and it is what makes `Tensor` one ArrayType instead
 * of a second type system (plan sections 3.1 and 5.1).
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
    /// spelling a bare `..` produces; the plan keeps it for printing `Tensor<f32>`).
    static ArrayType *openRank(Type *elemType, std::span<const ExtentForm> prefix = {});

    /// The numeric-unknown array `Tensor`: element type not fixed, but a numeric scalar. With
    /// no extents it is `Tensor` (any rank); with extents it is `Tensor<[...]>`.
    static ArrayType *numericUnknown(std::span<const int64_t> extents = {}, std::string_view pack = {});

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
    /// The numeric-unknown array (`Tensor`) marker. (The factory is `numericUnknown()`; a
    /// member of the same name would make `array->numericUnknown()` ambiguous with it.)
    bool isNumericUnknown() const { return numericUnknown_; }

    /// This type with another element type, shape and flags unchanged.
    ArrayType *withElementType(Type *elemType) const;

    /// The bracket text of the shape: `[]`, `[3, 4]`, `[?, 3]`, `[3, ..rest]`, `[..]`.
    std::string shapeText() const;
    /// The same in the tensor module's spelling, where an unknown dimension is written (`[?]`,
    /// not `[]`); `Tensor<[?]>` and `int[]` are the two spellings the same extent has.
    std::string tensorShapeText() const;
    /// The shape for the mangled name: rank (`any` or a number) and constant extents; dimension
    /// names and pack names do not appear, because they do not take part in equality.
    std::string shapeMangle() const;

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

    std::string_view packName() const override;
    bool extentForms(std::vector<ExtentForm> &out) const override;
    bool openRank() const override { return anyRank_; }
    const Type *
    withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const override;

  private:
    // Private constructor: called by the factory or create.
    ArrayType(
        Type *elemType, std::vector<ExtentForm> extents, bool anyRank,
        const std::string *packName, bool numericUnknown);

    /// Interned construction after normalization and validation.
    static ArrayType *make(
        Type *elemType, std::vector<ExtentForm> extents, bool anyRank, std::string_view pack,
        bool numericUnknown);
    /// The shape text; `tensorSpelling` writes `[?]` where the array spelling writes `[]`.
    std::string shapeText(bool tensorSpelling) const;

    Type *elemType_;        // Element type pointer (used by the type system).
    TypeCode elemTypeCode_; // Element type code (used at runtime).
    std::vector<ExtentForm> extents_;
    bool anyRank_;
    bool numericUnknown_;
    const std::string *packName_; // interned, nullptr for an anonymous pack or no pack
};

/// The array type of `type`, or nullptr when `type` is not an array. The counterpart of the
/// per-composite `as...Type` casts, for the code that used to ask a module whether a value was
/// its own kind of type (plan section 5.3).
const ArrayType *asArrayType(const Type *type);

/// True for an array whose element is a numeric scalar, or is not fixed yet but is known to be
/// one (the numeric-unknown array, `Tensor`): the arrays a tensor operator takes. A nested array
/// (`f32[][]`) is an array of arrays, not one numeric array, and is not one of these.
bool isNumericArrayType(const Type *type);

} // namespace camel::core::type