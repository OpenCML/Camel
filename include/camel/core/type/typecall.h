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
    Type *armResult(size_t arm) const;

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
        std::vector<uint16_t> shapes, std::vector<uint32_t> identityCounts, size_t paramCount,
        Type **params)
        : OtherType(typeCode(), paramCount, params), shapes_(std::move(shapes)),
          identityCounts_(std::move(identityCounts)) {}

    size_t firstParamOf(size_t arm) const;

    std::vector<uint16_t> shapes_;
    std::vector<uint32_t> identityCounts_;
};

/// Whether a type computed from types (a primitive application or a `match type`) occurs
/// anywhere in `type`.
bool containsTypeCall(Type *type);

} // namespace camel::core::type
