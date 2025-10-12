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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composed.h"

class ArrayType : public ComposedType {
  private:
    type_ptr_t elementType_;

  public:
    ArrayType(const type_ptr_t &elementType);

    type_ptr_t elementType() const;

    std::string toString() const override;

    virtual std::string mangle() const override;

    std::optional<type_ptr_t> typeAt(struct_idx_t idx) const override;

    virtual bool resolved() const override;
    virtual void resolve(const type_vec_t &typeList) override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    virtual std::shared_ptr<ComposedType> clone() const override;

    CastSafety castSafetyTo(const Type &other) const override;
};
