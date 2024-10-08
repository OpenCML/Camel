/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Oct. 6, 2024
 * Updated: Oct. 07, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "struct.h"

class TensorType : public StructType {
  private:
    type_ptr_t elementType_;
    std::vector<size_t> shape_;

  public:
    TensorType(const type_ptr_t &elementType, const std::vector<size_t> &shape);

    std::vector<size_t> shape() const { return shape_; }
    type_ptr_t elementType() const { return elementType_; }

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};
