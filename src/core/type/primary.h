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

#include "type.h"

class PrimaryType : public Type {
  public:
    PrimaryType() = delete;
    PrimaryType(TypeCode code) : Type(code) {}

    virtual bool operator==(const Type &other) const override;
    virtual bool operator!=(const Type &other) const override;

    virtual type_ptr_t clone() const override;

    virtual std::string mangle() const override;

    CastSafety castSafetyTo(const Type &other) const override;
};
