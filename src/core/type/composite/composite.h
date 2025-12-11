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
 * Created: Oct. 06, 2024
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/type/base.h"

#include <optional>

class CompositeType : public Type {
  public:
    CompositeType() = delete;
    CompositeType(TypeCode code) : Type(code) {}
    virtual ~CompositeType() noexcept = default;

    // 不修改原类型，返回新的类型
    virtual type_ptr_t resolve(const type_vec_t &typeList) const = 0;
    virtual bool resolved() const                                = 0;
    virtual std::optional<type_ptr_t> typeAt(size_t index) const = 0;

    virtual std::string toString() const override                     = 0;
    virtual std::string mangle() const override                       = 0;
    virtual type_ptr_t clone(bool deep = false) const override        = 0;
    virtual bool equals(const type_ptr_t &type) const override        = 0;
    virtual CastSafety castSafetyTo(const Type &other) const override = 0;
    virtual bool assignable(const type_ptr_t &type) const override    = 0;
};
