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

#include <regex>

#include "../data.h"

#include "../primary.h"

class StringData : public Data {
  private:
    std::string data_;

  public:
    StringData() = delete;
    StringData(const std::string &data);

    const std::string &data() const;

    virtual bool equals(const data_ptr_t &other) const override;

    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};
