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

#pragma once

#include "struct.h"

class DictData : public StructData {
  private:
    std::vector<std::string> refs_;
    std::unordered_map<std::string, data_ptr_t> data_;

  public:
    DictData();
    DictData(std::initializer_list<std::pair<std::string, data_ptr_t>> data);
    virtual ~DictData() = default;

    bool emplace(const std::string &key, const data_ptr_t &val);

    bool add(const std::string &key, const data_ptr_t &val);
    bool del(const std::string &key);
    bool has(const std::string &key) const;
    void set(const std::string &key, const data_ptr_t &val);
    data_ptr_t get(const std::string &key) const;

    void clear() {
        DictType &dictType = *static_cast<DictType *>(type_.get());
        dictType.clear();
        data_.clear();
    }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refs_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};