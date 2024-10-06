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

class TupleType : public StructType {
  private:
    std::vector<type_ptr_t> types_;

  public:
    TupleType();
    TupleType(const std::initializer_list<type_ptr_t> &types);
    TupleType(const std::vector<type_ptr_t> &types);

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    void add(const type_ptr_t &type);
    void set(size_t index, const type_ptr_t &type);

    size_t size() const;

    const std::vector<type_ptr_t> &types() const;

    TypeConv convertibility(const Type &other) const override;
};