// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type/base.h"

#include <optional>

namespace camel::core::type {

class CompositeType : public Type {
  public:
    CompositeType() = delete;
    CompositeType(TypeCode code) : Type(code) {}
    virtual ~CompositeType() noexcept = default;

    // Do not mutate the original type; return a new type instead.
    virtual Type *resolve(const type_vec_t &typeList) const = 0;
    virtual bool resolved() const                           = 0;

    virtual std::string toString() const override                      = 0;
    virtual std::string mangle() const override                        = 0;
    virtual Type *clone(bool deep = false) const override              = 0;
    virtual bool equals(Type *type) const override                     = 0;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override = 0;
    virtual bool assignableFrom(Type *sourceType) const                = 0;
};

} // namespace camel::core::type
