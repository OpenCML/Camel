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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/other.h"

/** Python 模块导出的类型：表示 Python 对象句柄，由 exportType 导出。 */
class PyObjectType : public OtherType {
  public:
    explicit PyObjectType(TypeCode code) : OtherType(code) {}

  protected:
    PyObjectType(TypeCode code, size_t paramCount, Type **params);

  public:
    static TypeCode typeCode();
    static PyObjectType *create();

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    CastSafety castSafetyTo(Type *targetType) const override;
    slot_t castSlotTo(slot_t value, Type *targetType) const override;
    bool assignable(Type *type) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;
};

PyObjectType *getPyObjectType();
