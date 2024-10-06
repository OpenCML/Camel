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

class NullData : public Data {
  public:
    NullData(type_ptr_t type = voidTypePtr) : Data(type) {}
    virtual ~NullData() = default;

    virtual bool equals(const data_ptr_t &other) const override { return true; }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        throw DataConvError("Cannot convert null to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<NullData>(); }
    virtual const std::string toString() const override { return "null"; }
};