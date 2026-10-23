// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/slot.h"
#include "camel/core/type/extent.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::type {

namespace type_impl = ::camel::core::type;

/*
 TypeCode (32 bits) encoding layout:

 bit31      bit30      bit29      bit28      bit27 ............... bit0
 +----------+----------+----------+----------+------------------------+
 |OtherType |Composite |GC_Traced |Auxiliary |      ID (28 bits)      |
 +----------+----------+----------+----------+------------------------+
 ^          ^          ^          ^          ^
 |          |          |          |          └─ Class-unique ID (0x0000000 ~ 0x0FFFFFFF)
 |          |          |          └─ bit28: auxiliary type (no actual data storage)
 |          |          └─ bit29: GC-traced type flag
 |          └─ bit30: composite type flag (Struct / Array / Tuple / ...)
 └─ bit31: third-party type flag (non-built-in type)

 Notes:
 - High-bit flags can be combined, e.g. Composite | GC_Traced
 - ID distinguishes concrete type numbers; only the low 28 bits are used
*/

enum class TypeFlag : uint32_t {
    Primitive = 0,        // Built-in primitive type: neither composite nor GC-traced.
    OtherType = 1u << 31, // Third-party type; actual IDs are registered at runtime.
    Composite = 1u << 30, // Composite type made from multiple types.
    GC_Traced = 1u << 29, // GC-traced type whose data object holds GC-managed references.
    Auxiliary = 1u << 28, // Auxiliary type used for resolution but with no data object.
    // Reference = 1u << 27  // Reserved reference type.
};

constexpr TypeFlag operator|(TypeFlag a, TypeFlag b) {
    return static_cast<TypeFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr uint32_t makeTypeCode(TypeFlag flags, uint32_t id) {
    return static_cast<uint32_t>(flags) | (id & 0x0FFFFFFF); // Store the ID in the low 28 bits.
}

enum class TypeCode : uint32_t {
    Int32    = makeTypeCode(TypeFlag::Primitive, 0x000),
    Int64    = makeTypeCode(TypeFlag::Primitive, 0x001),
    Float32  = makeTypeCode(TypeFlag::Primitive, 0x002),
    Float64  = makeTypeCode(TypeFlag::Primitive, 0x003),
    Bool     = makeTypeCode(TypeFlag::Primitive, 0x004),
    Void     = makeTypeCode(TypeFlag::Primitive, 0x005),
    String   = makeTypeCode(TypeFlag::Primitive | TypeFlag::GC_Traced, 0x006),
    Array    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x007),
    Tuple    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x008),
    Struct   = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x009),
    Function = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x00A),
    Ref      = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00B),
    // 0x00C was `any`, removed (design principles 5.10): no value has a type that is
    // undetermined after compilation. 0x00D was the union type `A | B`, retired (5.3): a sum
    // of types is an `enum`.
    // Compile-time only (camel/core/type/typevar.h): a type variable of a generic function
    // or of a parameterised type declaration, and a parameterised type declaration itself.
    // Neither is the type of a node of a graph.
    TypeVar  = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00E),
    TypeFunc = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00F),
    // The argument of a pending enum instance that its value does not determine (`Option<_>`).
    // Compile time only: no value has this type.
    EnumHole = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x010),
    // `typeof e` before e is built (design principles 5.6): the type of the value the expression
    // will have. Compile time only; the graph builder replaces it by the expression's type.
    TypeOf = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x013),
    // Complex numbers (design principles 5.1): two IEEE parts, `c64` (two f32) and `c128` (two
    // f64). They are numbers, but not primitive in the encoding's sense: a value does not fit
    // in one slot (c128 is 16 bytes, c64 needs a box so that a number is one kind of value),
    // so the data object is GC-traced like a String. `isComplex` below is the predicate to
    // ask instead of `isPrimitive`.
    Complex64  = makeTypeCode(TypeFlag::Primitive | TypeFlag::GC_Traced, 0x011),
    Complex128 = makeTypeCode(TypeFlag::Primitive | TypeFlag::GC_Traced, 0x012),
    // Reduced-precision floating types (design principles 7.3, H238): `bf16` and `f16`, the
    // element types of `Tensor<bf16>` and `Tensor<f16>` and scalar types of the language as
    // well — a literal has one, and it has the written conversion, the arithmetic and the
    // comparisons of any other float (camel/core/rtdata/half.h). A value of one fits in a slot
    // as its two-byte pattern in the low half, computed on as f32 and rounded once on store.
    // The bit patterns are not the low 16 bits of their type code, so a consumer that indexes a
    // table by `code & 0xF` must handle them first (castSafetyFrom does).
    BFloat16 = makeTypeCode(TypeFlag::Primitive, 0x014),
    Float16  = makeTypeCode(TypeFlag::Primitive, 0x015),
    Other    = makeTypeCode(TypeFlag::OtherType, 0x000),
};

inline constexpr bool hasFlag(uint32_t code, TypeFlag flag) {
    return (code & static_cast<uint32_t>(flag)) != 0;
}

// Note: isPrimitive means
// !isComposite && !isOtherType && !isAuxiliary
template <typename T> inline constexpr bool isPrimitive(T code) {
    // Primitive types have no flags set, i.e. the upper four bits are all 0.
    return (static_cast<uint32_t>(code) & 0xF0000000) == 0;
}

template <typename T> inline constexpr bool isOtherType(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::OtherType);
}

template <typename T> inline constexpr bool isComposite(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Composite);
}

template <typename T> inline constexpr bool isGCTraced(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::GC_Traced);
}

template <typename T> inline constexpr bool isAuxiliary(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Auxiliary);
}

/// A complex number type (`c64`, `c128`). Complex values are numbers but are boxed and
/// GC-traced, so `isPrimitive` is false for them; every numeric test that must include them
/// asks this.
template <typename T> inline constexpr bool isComplex(T code) {
    return code == TypeCode::Complex64 || code == TypeCode::Complex128;
}

/// A floating-point scalar type: `f32`, `float`, and the two reduced-precision floating types
/// `bf16` and `f16`, which are floating types of the language with a range and a precision of
/// their own (design principles 7.3). Every test that asks "is this floating point" asks this,
/// not `code == Float32 || code == Float64`: a question about a family must name the family, or
/// the next member of it is silently left out of the answer.
template <typename T> inline constexpr bool isFloatingScalar(T code) {
    return code == TypeCode::Float32 || code == TypeCode::Float64 || code == TypeCode::BFloat16 ||
           code == TypeCode::Float16;
}

/// A numeric scalar type: an integer or a floating-point one (`isFloatingScalar`), the complex
/// numbers excluded as `isComplex` excludes them everywhere else.
template <typename T> inline constexpr bool isNumericScalar(T code) {
    return code == TypeCode::Int32 || code == TypeCode::Int64 || isFloatingScalar(code);
}

enum class CastSafety {
    Safe      = 1,
    Unsafe    = -1,
    Forbidden = 0,
};

/// The name of a type as a program writes it (design principles 5.1): `int` and `float`
/// for the 64-bit integer and floating-point types, which the compiler knows by their width
/// (`i64`, `f64`; a program may write those too). This function and sourceTypeSpelling are
/// the one place the mapping is written: everything that prints a type (diagnostics, the
/// names and annotations of graphs, `inspect`, the language server) prints this.
std::string typeCodeToString(TypeCode code);
/// `text` with the type names the compiler uses internally replaced by the names of source
/// text, word by word (`(x: i64) => f64` gives `(x: int) => float`). For signatures written
/// by hand as text (the type rules of built-in operators); a Type prints itself that way.
std::string sourceTypeSpelling(std::string_view text);

extern const signed char primitiveTypeConvMatrix[6][6];

class Type;
using type_vec_t = std::vector<Type *>;

/// One argument of a parameterised type (camel/core/type/other.h): a type, a compile-time
/// constant, or a list of extents over dimension names. Declared here because `Type` routes
/// `Name<args...>` to the type itself (`applyArguments`), and defined with the module types.
struct TypeArgument;

/// Index of a type in the TypeTable (camel/core/type/intern.h). Equal non-zero ids mean the
/// same type object.
using TypeId = uint32_t;
/// The id of a type that is not in the table.
inline constexpr TypeId kNoTypeId = 0;

class Type {
    friend class TypeTable;

  protected:
    TypeCode code_;

  private:
    // Set once by the TypeTable, before the object is handed to anyone else.
    TypeId typeId_ = kNoTypeId;

  public:
    Type() = delete;
    Type(TypeCode type);
    virtual ~Type() noexcept = default;

    const TypeCode &code() const;

    /// The id of this type in the TypeTable, or kNoTypeId when it is not interned (see
    /// intern.h for which types are). Lock-free.
    TypeId typeId() const { return typeId_; }
    /// True when the TypeTable holds this object: it is the one object of its structure (as
    /// far as its components are interned) and must not be modified.
    bool isInterned() const { return typeId_ != kNoTypeId; }

    inline bool isPrimitive() const { return type_impl::isPrimitive(code_); }
    inline bool isOtherType() const { return type_impl::isOtherType(code_); }
    inline bool isComposite() const { return type_impl::isComposite(code_); }
    inline bool isGCTraced() const { return type_impl::isGCTraced(code_); }
    inline bool isAuxiliary() const { return type_impl::isAuxiliary(code_); }

    virtual std::string toString() const;
    virtual std::string mangle() const;
    virtual Type *clone(bool deep = false) const;

    virtual bool equals(Type *type) const;
    /**
     * Least common type of `this` and `other`, used where control flow merges
     * values (branch joins). The default accepts only equal types and returns
     * nullptr otherwise; types with a refinement lattice (for example tensors
     * with partially known shapes) override it to widen instead of failing.
     */
    virtual Type *unify(Type *other) const;
    /**
     * The type given to a mutable (`var`) binding initialized with a value of
     * this type. Refinement-carrying types widen to their general form so that
     * later assignments are not restricted to the initializer's exact
     * refinement. The default is the type itself.
     */
    virtual Type *widened() const;
    /**
     * The type a literal operand takes beside a value of this type (design principles 5.1): in a
     * binary operation the literal takes the type of the other operand, and the value an
     * operation on a numeric array is about is an element of it (`x * 2.0` with `x` an `f32`
     * tensor is the f32 product `x * 2.0f`, not an f64 product that no device program has).
     * A numeric scalar gives itself; a numeric array gives its element type; a type that fixes
     * neither (a `Tensor` whose element type is open, a `string`) gives none, and the literal
     * keeps its own type.
     */
    virtual Type *literalOperandType() const;
    virtual CastSafety castSafetyFrom(Type *sourceType) const;
    virtual slot_t castSlotFrom(slot_t value, Type *sourceType) const;
    bool assignableFrom(Type *sourceType) const;

    /**
     * The dimensions this type states, in declaration order, as compile-time extents (design
     * principles 5.4): a constant, unknown, or a linear form over dimension names. Returns
     * false when the type has no dimensions to state (the default). A type with a trailing
     * pack (`T[n, ..rest]`) reports its prefix here and the pack name through packName();
     * `openRank()` says whether the prefix covers the whole shape.
     */
    virtual bool extentForms(std::vector<ExtentForm> &out) const;
    /// The name of the trailing pack (`T[..rest]`), empty for an anonymous pack or none.
    virtual std::string_view packName() const;
    /**
     * True when the rank is not fixed by this type (`T[..]`, `T[3, ..]`): extents() is then a
     * prefix that any rank at least that long satisfies. The call site relies on it to tell
     * an argument whose trailing dimensions are unknown, where it binds no pack.
     */
    virtual bool openRank() const;
    /**
     * This type with the extent names in `values` replaced by their bound extents and the pack
     * names in `packs` replaced by the dimensions they stood for at a call. Returns `this`
     * when nothing was bound. A type that loses its last dimension this way (a bare pack
     * `T[..rest]` bound to no dimensions) denotes its element type, which is a scalar.
     */
    virtual const Type *
    withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const;
    /**
     * `Name<args...>`: the type these arguments denote. The default refuses every argument
     * (`this type takes no arguments`). A type whose arguments carry meaning overrides it.
     */
    virtual const Type *
    applyArguments(std::span<const TypeArgument> arguments, std::string &error) const;

    bool operator==(Type *other) const = delete;
    bool operator!=(Type *other) const = delete;

    static Type *create(TypeCode code);
    static Type *Int32();
    static Type *Int64();
    static Type *Int();
    static Type *Float32();
    static Type *Float64();
    static Type *Float();
    static Type *Bool();
    static Type *Void();
    static Type *String();
    static Type *Complex64();
    static Type *Complex128();
    static Type *BFloat16();
    static Type *Float16();
    static Type *Ref();
};

/// What binding the dimension names of one argument did (design principles 5.4).
enum class ExtentBinding {
    /// The parameter states no dimension names (or the actual type carries none the parameter
    /// can be bound from): the ordinary argument check decides, as it did before.
    NotDimensioned,
    /// Every name the actual type fixes has been bound into `values`/`packs`.
    Bound,
    /// A name was already bound to a different extent: `mismatch` holds the actual extent.
    Mismatch,
};

/**
 * Binds the dimension names a parameter type states from one argument type (`T[n, ..rest]`
 * against `f32[3, 4, 5]` binds `n = 3` and `rest = [4, 5]`). `values` and `packs` accumulate
 * over the arguments of one call, so a dimension can be fixed by a later argument than the one
 * that mentions it (`f(a: f32[?, n], b: f32[n])`). Extents the argument leaves unknown bind
 * nothing; the checked conversion at the call then enforces them when it runs.
 *
 * The rules are those of design principles 5.4 as the compiler already applied them to tensor
 * types: a constant argument extent binds, an unknown one does not, a contradiction is a
 * mismatch, and a pack is bound only when the argument's rank is fixed.
 */
ExtentBinding bindExtents(
    const Type *parameter, const Type *actual, ExtentValues &values, ExtentPacks &packs,
    std::string &mismatch);

} // namespace camel::core::type
