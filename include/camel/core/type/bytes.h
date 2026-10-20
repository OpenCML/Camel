/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// One core-owned immutable binary type shared by file, tensor and codec modules.
#pragma once
#include "camel/core/type/other.h"
namespace camel::core::type {
class BytesType final : public OtherType {
  public:
    static BytesType *get();
    static TypeCode typeCode();
    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *other) const override;
    bool assignableFrom(Type *other) const override;
    CastSafety castSafetyFrom(Type *other) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    BytesType();
};
bool isBytesType(const Type *type);
} // namespace camel::core::type
