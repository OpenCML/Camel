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

class MapData : public StructData {
  private:
    std::vector<std::pair<data_ptr_t, bool>> refs_; // bool indicates if the ptr is key
    // TODO: need to implement a hash function for data_ptr_t
    std::unordered_map<data_ptr_t, data_ptr_t> data_;

  public:
    MapData(type_ptr_t keyType, type_ptr_t dataType);
    virtual ~MapData() = default;

    bool emplace(const data_ptr_t &key, const data_ptr_t &val);

    bool set(const data_ptr_t &key, const data_ptr_t &val);
    bool del(const data_ptr_t &key);
    data_ptr_t get(const data_ptr_t &key) const;

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refs_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t as(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
