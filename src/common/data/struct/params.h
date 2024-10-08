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
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "struct.h"

class ParamsData : public StructData {
  private:
    std::vector<std::pair<size_t, std::string>> refs_;
    std::vector<data_ptr_t> indexData_;
    std::map<std::string, data_ptr_t> namedData_;

  public:
    ParamsData();
    virtual ~ParamsData() = default;

    bool emplace(const data_ptr_t &val, const std::string &key = "");

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refs_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    data_vec_t &indexData() { return indexData_; }
    std::map<std::string, data_ptr_t> &namedData() { return namedData_; }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;

    data_ptr_t convertToMap();
    data_ptr_t convertToList();
    data_ptr_t convertToTuple();
    data_ptr_t convertToParams(std::shared_ptr<ParamsType> &target, bool inplace = false);
};
