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
 * Updated: Oct. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "struct.h"

class ListData : public StructData {
  private:
    std::vector<size_t> refs_;
    std::vector<data_ptr_t> data_;

  public:
    ListData();
    ListData(data_list_t data);
    virtual ~ListData() = default;

    void emplace(const data_ptr_t &e);

    void pushBack(const data_ptr_t &e);
    data_ptr_t popBack();

    data_ptr_t get(size_t index) const;
    bool set(size_t index, const data_ptr_t &e);

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refs_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;

    data_ptr_t convertToParams(const std::shared_ptr<ParamsType> &target);
};
