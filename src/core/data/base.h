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
 * Updated: Dec. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/type/type.h"
#include "utils/type.h"

class Data;
using data_ptr_t  = std::shared_ptr<Data>;
using data_vec_t  = std::vector<data_ptr_t>;
using data_list_t = std::initializer_list<data_ptr_t>;

class Data : public std::enable_shared_from_this<Data> {
  protected:
    type_ptr_t type_;

  public:
    Data();
    Data(type_ptr_t type);
    virtual ~Data() = default;

    type_ptr_t type() const;
    static data_ptr_t null();

    virtual bool equals(const data_ptr_t &other) const = 0;
    virtual data_ptr_t clone(bool deep = false) const  = 0;
    virtual const std::string toString() const         = 0;
};
