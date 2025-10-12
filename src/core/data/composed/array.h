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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composed.h"

class ArrayData : public ComposedData {
  private:
    std::vector<size_t> refIndices_;
    std::vector<data_ptr_t> data_;

  public:
    ArrayData(type_ptr_t arrType, data_list_t data = {});
    ArrayData(type_ptr_t arrType, data_vec_t &&data);
    virtual ~ArrayData() = default;

    static std::shared_ptr<ArrayData> create(type_ptr_t arrType, data_list_t data = {}) {
        return std::make_shared<ArrayData>(arrType, data);
    }
    static std::shared_ptr<ArrayData> from(type_ptr_t arrType, data_vec_t &&data) {
        return std::make_shared<ArrayData>(arrType, std::move(data));
    }

    void emplace(const data_ptr_t &e);

    std::vector<data_ptr_t> &raw() { return data_; }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
