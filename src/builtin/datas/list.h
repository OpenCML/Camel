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

#include "builtin/types/list.h"
#include "core/data/other/other.h"

class ListData : public OtherData {
  private:
    std::vector<size_t> refIndices_;
    std::vector<data_ptr_t> data_;

  public:
    ListData();
    ListData(data_list_t data);
    ListData(data_vec_t &&data);
    virtual ~ListData() = default;

    static std::shared_ptr<ListData> create(data_list_t data) {
        return std::make_shared<ListData>(data);
    }
    static std::shared_ptr<ListData> create(data_vec_t &&data) {
        return std::make_shared<ListData>(std::move(data));
    }

    void emplace(const data_ptr_t &e);

    void pushBack(const data_ptr_t &e);
    data_ptr_t popBack();

    data_ptr_t get(size_t index) const;
    bool set(size_t index, const data_ptr_t &e);
    std::vector<data_ptr_t> &raw() { return data_; }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
