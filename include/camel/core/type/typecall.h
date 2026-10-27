// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A type function applied in a type position (design principles 21): `element_of::<T>()`,
 * `payload_of::<T>()`, `key_of::<T>()` and `value_of::<T>()` compute a type from a type, at
 * compile time, and are written where a type argument is: `is_json::<element_of::<T>()>()`.
 *
 * While an argument is still a type variable the call stays a type of its own, an application of
 * the function to its arguments, and the type-variable algebra descends through it like any other
 * type application. When the arguments are all concrete, which is when the variables of a generic
 * instance are bound, `substituteTypeVars` replaces the call by the type it computes. A call whose
 * arguments are concrete and which the function does not accept (`element_of::<int>()`) is
 * reported where it is written or instantiated.
 */

#pragma once

#include "camel/core/type/other.h"

#include <span>
#include <string>
#include <string_view>

namespace camel::core::type {

class TypeCallType final : public OtherType {
  public:
    static TypeCode typeCode();
    static TypeCallType *create(std::string_view name, std::span<Type *const> arguments);

    /// Whether `name` is one of the type functions.
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

/// Whether a type function applied to arguments occurs anywhere in `type`.
bool containsTypeCall(Type *type);

} // namespace camel::core::type
