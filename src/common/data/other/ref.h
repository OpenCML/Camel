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

class RefData : public Data {
  private:
    std::string ref_;

  public:
    RefData(const std::string &ref) : Data(refTypePtr), ref_(ref) {}
    virtual ~RefData() = default;

    const std::string &ref() const { return ref_; }

    virtual bool equals(const data_ptr_t &other) const override { return false; }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        throw DataConvError("Cannot convert RefData to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<RefData>(ref_); }
    virtual const std::string toString() const override { return "DREF<" + ref_ + ">"; }
};

using ref_data_ptr_t = std::shared_ptr<RefData>;