/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Mar. 10, 2026
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/other.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace camel::tensor {

namespace type = camel::core::type;

class TensorType : public type::OtherType {
  public:
    TensorType(const std::vector<size_t> &shape, type::Type *elementType = nullptr);

  protected:
    TensorType(type::TypeCode code, size_t paramCount, type::Type **params);

  public:
    static type::TypeCode typeCode();
    static TensorType *create(type::Type *elementType, const std::vector<size_t> &shape);
    static TensorType *Dynamic(type::Type *elementType = nullptr);
    static type::Type *Default();

    std::vector<size_t> shape() const;
    type::Type *dType() const;

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    std::vector<size_t> shape_;
    type::Type *elementType_;
};

TensorType *getTensorType(type::Type *dtype = nullptr);

} // namespace camel::tensor
