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
 * Updated: Oct. 05, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "struct.h"

class DictData : public StructData {
  private:
    std::vector<std::string> refIndices_;
    std::unordered_map<std::string, data_ptr_t> data_;

  public:
    DictData();
    DictData(std::initializer_list<std::pair<std::string, data_ptr_t>> data);
    DictData(std::unordered_map<std::string, data_ptr_t> &&data);
    virtual ~DictData() = default;

    static std::shared_ptr<DictData>
    create(std::initializer_list<std::pair<std::string, data_ptr_t>> data) {
        return std::make_shared<DictData>(data);
    }
    static std::shared_ptr<DictData> create(std::unordered_map<std::string, data_ptr_t> &&data) {
        return std::make_shared<DictData>(std::move(data));
    }

    bool emplace(const std::string &key, const data_ptr_t &val);

    bool add(const std::string &key, const data_ptr_t &val);
    bool del(const std::string &key);
    bool has(const std::string &key) const;
    void set(const std::string &key, const data_ptr_t &val);
    data_ptr_t get(const std::string &key) const;
    std::unordered_map<std::string, data_ptr_t> &raw() { return data_; }

    void clear() {
        DictType &dictType = *static_cast<DictType *>(type_.get());
        dictType.clear();
        data_.clear();
    }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
