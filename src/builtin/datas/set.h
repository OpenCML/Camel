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
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "builtin/types/set.h"
#include "core/data/other/other.h"

class SetData : public OtherData {
  private:
    std::vector<data_ptr_t> refDatas_;
    // TODO: need to implement a hash function for data_ptr_t
    std::unordered_set<data_ptr_t> data_;

  public:
    SetData(type_ptr_t elType);
    SetData(type_ptr_t elType, data_list_t data);
    SetData(type_ptr_t setType, std::unordered_set<data_ptr_t> &&data);
    virtual ~SetData() = default;

    static std::shared_ptr<SetData> create(type_ptr_t elType) {
        return std::make_shared<SetData>(elType);
    }
    static std::shared_ptr<SetData> create(type_ptr_t elType, data_list_t data) {
        return std::make_shared<SetData>(elType, data);
    }
    static std::shared_ptr<SetData>
    create(type_ptr_t setType, std::unordered_set<data_ptr_t> &&data) {
        return std::make_shared<SetData>(setType, std::move(data));
    }

    // append element to the set during construction
    bool emplace(const data_ptr_t &e);

    bool add(const data_ptr_t &e);
    bool del(const data_ptr_t &e);

    std::unordered_set<data_ptr_t> &raw() { return data_; }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refDatas_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
