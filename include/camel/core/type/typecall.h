// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Types computed from types at compile time (design principles 5.6, 21): a type function is
 * written with `type` and called with `F<...>` in a type position, and its body is a type
 * expression, a `match type` among them.
 *
 *   - `TypeCallType` is a primitive type function applied to arguments: `ElementOf<T>`,
 *     `PayloadOf<T>`, `KeyOf<T>` and `ValueOf<T>` of the module `types`, which take a type apart
 *     where `match type` cannot (the library declares them without a body).
 *   - `MatchTypeType` is a `match type` in a type position: the subject, and for each case the
 *     shapes and identities it selects and the type it gives.
 *
 * While an argument or the subject is still a type variable the application stays a type of its
 * own, and the type-variable algebra descends through it like any other type application. When
 * everything is concrete, which is when the variables of a generic instance are bound,
 * `substituteTypeVars` replaces it by the type it computes. One that stays unevaluated with
 * concrete arguments (`ElementOf<int>`, a `match type` no case selects) is reported where the
 * type is used.
 */

#pragma once

#include "camel/core/type/other.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::type {

class TypeCallType final : public OtherType {
  public:
    static TypeCode typeCode();
    static TypeCallType *create(std::string_view name, std::span<Type *const> arguments);

    /// Whether `name` is one of the primitive type functions.
    static bool isTypeFunction(std::string_view name);
    /// The type the function computes from concrete arguments; nullptr with `error` set when it
    /// does not accept them.
    static Type *
    evaluate(std::string_view name, std::span<Type *const> arguments, std::string &error);

    const std::string &name() const { return *name_; }

    /// Type packs (design principles 5.6, program transforms): a pack is the application
    /// `$pack<T1, T2, ...>` of its members, what `ParamsOf<F>` and `WithOf<F>` compute and what a
    /// pack variable (`A` of `::<..A>`) is bound to; `..E` written in a type is the application
    /// `$expand<E>`, which becomes one member per element of the packs `E` uses when they are
    /// bound (substituteTypeVars). `$lengths:...` is what an expansion becomes when the packs it
    /// uses have different lengths, and stays so to be reported where the type is used.
    static TypeCallType *makePack(std::span<Type *const> members);
    static TypeCallType *makeExpand(Type *pattern);
    static TypeCallType *makeLengthMismatch(const std::string &description);
    static bool isPack(const Type *type);
    static bool isExpand(const Type *type);
    /// The members of a pack, or of nothing when `type` is not one.
    static std::span<Type *const> packMembers(const Type *type);

    bool typeArguments() const override { return true; }
    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    bool assignableFrom(Type *sourceType) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    TypeCallType(const std::string *name, size_t paramCount, Type **params)
        : OtherType(typeCode(), paramCount, params), name_(name) {}

    const std::string *name_;
};

/// One case of a `match type` in a type position.
struct MatchTypeArm {
    /// The shapes the case names, as bits numbered like `TypeCaseKind` (Struct 0, Tuple 1, Leaf 2,
    /// Wildcard 3, Func 4, Array 6, Nullable 7, Fallible 8, Map 9, Set 10).
    uint16_t shapes = 0;
    /// The types the case names by identity.
    std::vector<Type *> identities;
    /// The patterns the case writes to take a type pack apart, one for each alternative of `|`
    /// (`case (W, ...Rest)`): a tuple type whose members are the pattern's members and whose last
    /// one is `...Rest` (design principles 5.6, where a type pack takes apart the same way a value
    /// pack does). Such a case selects a pack whose members the pattern fits, and the names it
    /// writes stand for those members. The head of a pack is this pattern in the standard library
    /// and nothing else (`stdlib/types.cml` declares it as a `match type`).
    std::vector<Type *> packPatterns;
    /// The patterns the case writes to take an *array* type apart, one for each alternative of `|`
    /// (`case E[...Dims]`, `case E[...]`): an array type with no extent before its dimensions pack
    /// whose element type is the name the case binds (an array whose first dimension is an array
    /// is still one array, so `E` is that element and `Dims` the dimensions after it).
    ///
    /// The two spellings select the two things a shape states. A *named* pack (`case E[...Dims]`)
    /// binds the dimensions themselves, so it selects an array whose rank is stated: the name
    /// stands for the dimensions as a dimension pack, and, where an extent is written, for how
    /// many there are -- what `ShapeOf<T>` needs (`int[Dims]` is `int[<the rank>]`). An
    /// *anonymous* pack (`case E[...]`) is the open rank: the value's rank is not stated, so there
    /// is nothing to bind and nothing to count. Design principles 5.4: `T[...]` is the type of
    /// every rank, and a dimension pack is the dimensions a type does not name.
    std::vector<Type *> arrayPatterns;
    /// The type the case gives.
    Type *result = nullptr;
};

/// The shapes a concrete type has, as the bits of `MatchTypeArm::shapes`: Struct or Tuple or
/// Leaf, Wildcard, and the others it is.
uint16_t typeShapeBits(const Type *type);

class MatchTypeType final : public OtherType {
  public:
    static TypeCode typeCode();
    static MatchTypeType *create(Type *subject, std::span<const MatchTypeArm> arms);

    Type *subject() const { return params_[0]; }
    size_t armCount() const { return shapes_.size(); }
    uint16_t armShapes(size_t arm) const { return shapes_[arm]; }
    std::span<Type *const> armIdentities(size_t arm) const;
    /// The patterns the case writes to take a pack apart; empty when it writes none.
    std::span<Type *const> armPackPatterns(size_t arm) const;
    /// The patterns the case writes to take an array type apart; empty when it writes none.
    std::span<Type *const> armArrayPatterns(size_t arm) const;
    Type *armResult(size_t arm) const;
    /// The names one case binds: what its patterns write for the subject's members -- the element
    /// type and the dimensions of an array pattern, the members and the rest of a pack pattern.
    /// They stand for what the case reached and are not type variables of the type the `match
    /// type` is part of (typevar.cpp `collectTypeVars`).
    void armBoundNames(size_t arm, std::vector<std::string> &names) const;

    bool typeArguments() const override { return true; }
    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    bool assignableFrom(Type *sourceType) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    MatchTypeType(
        std::vector<uint16_t> shapes, std::vector<uint32_t> identityCounts,
        std::vector<uint32_t> packCounts, std::vector<uint32_t> arrayCounts, size_t paramCount,
        Type **params)
        : OtherType(typeCode(), paramCount, params), shapes_(std::move(shapes)),
          identityCounts_(std::move(identityCounts)), packCounts_(std::move(packCounts)),
          arrayCounts_(std::move(arrayCounts)) {}

    size_t firstParamOf(size_t arm) const;

    std::vector<uint16_t> shapes_;
    std::vector<uint32_t> identityCounts_;
    std::vector<uint32_t> packCounts_;
    std::vector<uint32_t> arrayCounts_;
};

/// Whether a type computed from types (a primitive application or a `match type`) occurs
/// anywhere in `type`.
bool containsTypeCall(Type *type);

/// What stays unevaluated in `type` although its arguments are concrete, said for a diagnostic:
/// `what` is the application (`ElementOf<int>`, `match type int {...}`) and `why` the reason
/// (`int is neither an array nor a set`, `no case selects int`). False when nothing of the kind is
/// left.
bool describeUnevaluatedTypeCall(Type *type, std::string &what, std::string &why);

} // namespace camel::core::type
