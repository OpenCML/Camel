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
 * Updated: Oct. 18, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/type/base.h"

#include <optional>
#include <variant>

using struct_idx_t = std::variant<std::string, size_t>;

class ComposedType : public Type {
  public:
    ComposedType() = delete;
    ComposedType(TypeCode code) : Type(code) {}

    virtual std::string toString() const override = 0;

    virtual std::string mangle() const override = 0;

    virtual std::optional<type_ptr_t> typeAt(struct_idx_t idx) const = 0;

    virtual bool resolved() const {
        ASSERT(false, "resolved() not implemented");
        return false;
    };
    virtual void resolve(const type_vec_t &typeList) { ASSERT(false, "resolve() not implemented"); }

    virtual bool operator==(const Type &other) const override = 0;
    virtual bool operator!=(const Type &other) const override = 0;

    virtual type_ptr_t clone() const override = 0;

    virtual CastSafety castSafetyTo(const Type &other) const override = 0;
};
