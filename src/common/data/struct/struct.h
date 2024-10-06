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

#include "../data.h"

class StructData : public Data {
  public:
    StructData() = default;
    StructData(type_ptr_t type) : Data(type) {}
    virtual ~StructData() = default;

    virtual type_ptr_t type() const override { return type_; }

    virtual std::vector<std::string> refs() const override { return std::vector<std::string>(); }
    virtual bool resolved() const override { return true; }
    virtual void resolve(const data_vec_t &dataList) override {}

    virtual bool equals(const data_ptr_t &other) const override = 0;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override = 0;
    virtual data_ptr_t clone(bool deep = false) const override = 0;
    virtual const std::string toString() const override = 0;
};