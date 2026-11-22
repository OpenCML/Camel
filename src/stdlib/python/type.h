// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/other.h"

namespace type_ns = camel::core::type;

/** Python 模块导出的类型：表示 Python 对象句柄，由 exportType 导出。 */
class PyObjectType : public type_ns::OtherType {
  public:
    explicit PyObjectType(type_ns::TypeCode code) : type_ns::OtherType(code) {}

  public:
    static type_ns::TypeCode typeCode();
    static PyObjectType *create();

    std::string toString() const override;
    std::string mangle() const override;
    type_ns::Type *clone(bool deep = false) const override;
    bool equals(type_ns::Type *type) const override;
    type_ns::CastSafety castSafetyFrom(type_ns::Type *sourceType) const override;
    slot_t castSlotFrom(slot_t value, type_ns::Type *sourceType) const override;
    bool assignableFrom(type_ns::Type *sourceType) const override;
    type_ns::OtherType *cloneWithParams(std::span<type_ns::Type *const> params) const override;
};

PyObjectType *getPyObjectType();
