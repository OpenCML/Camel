/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

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