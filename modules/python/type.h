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
 * Created: Feb. 20, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/other.h"

namespace type_ns = camel::core::type;

/** Python 模块导出的类型：表示 Python 对象句柄，由 exportType 导出。 */
class PyObjectType : public type_ns::OtherType {
  public:
    explicit PyObjectType(type_ns::TypeCode code) : type_ns::OtherType(code) {}

  protected:
    PyObjectType(type_ns::TypeCode code, size_t paramCount, type_ns::Type **params);

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
