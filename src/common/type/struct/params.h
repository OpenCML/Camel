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

class ParamsType : public StructType {
  private:
    std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>> elements_;

    TypeConv convertibilityToParams(const ParamsType &other) const;

  public:
    ParamsType();

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    bool add(const std::string &key, const type_ptr_t &type, const data_ptr_t &value = nullptr);

    size_t size() const;
    const std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>> &elements() const;

    std::map<std::string, type_ptr_t> map() const;

    void clear();

    TypeConv convertibility(const Type &other) const override;
};