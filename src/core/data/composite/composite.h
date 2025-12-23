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
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../base.h"

class CompositeData : public Data {
  public:
    CompositeData() = default;
    CompositeData(type_ptr_t type) : Data(type) {}
    virtual ~CompositeData() = default;

    virtual std::vector<std::string> refs() const { return std::vector<std::string>(); }
    virtual bool resolved() const { return true; }
    virtual void resolve(const data_vec_t &dataList) {}

    virtual bool equals(const data_ptr_t &other) const override   = 0;
    virtual data_ptr_t clone(bool deep = false) const override    = 0;
    virtual const std::string toString() const override           = 0;
    virtual data_ptr_t convertTo(const type_ptr_t &type) override = 0;
};
