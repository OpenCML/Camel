/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Neural-network public runtime types.
 *
 * Parameter is a stable mutable object that owns tensor data and gradient
 * buffers. Models should aggregate Parameter values inside normal Camel
 * structs so macro-generated training code can discover and update them.
 */

#pragma once

#include "camel/core/type/other.h"

namespace camel::nn {

namespace type = camel::core::type;

class ParameterType : public type::OtherType {
  public:
    ParameterType();

    static type::TypeCode typeCode();
    static type::Type *Default();

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;
};

} // namespace camel::nn
