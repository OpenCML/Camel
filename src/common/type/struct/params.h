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
 * Created: Oct. 06, 2024
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

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
    std::vector<type_ptr_t> indexElements() const;
    std::unordered_map<std::string, type_ptr_t> namedElements() const;

    void clear();

    TypeConv convertibility(const Type &other) const override;
};
