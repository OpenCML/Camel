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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "struct.h"

class UnionType : public StructType {
  private:
    std::unordered_set<type_ptr_t> types_;

    void insertUnion(const UnionType &other);

  public:
    UnionType();
    UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs);
    UnionType(const std::initializer_list<type_ptr_t> &types);
    UnionType(const std::vector<type_ptr_t> &types);

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    void add(const type_ptr_t &type);
    bool has(const type_ptr_t &type) const;

    TypeConv convertibility(const Type &other) const override;
};
