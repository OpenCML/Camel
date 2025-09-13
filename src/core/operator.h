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
 * Created: Aug. 10, 2024
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/data/data.h"

#include <functional>
#include <unordered_map>

class Context;
class Operator;
class OperatorIndex;

using operator_t = std::function<data_ptr_t(Context &, data_vec_t &, data_vec_t &)>;

using oper_idx_ptr_t = std::shared_ptr<OperatorIndex>;
using oper_idx_vec_t = std::vector<oper_idx_ptr_t>;
using oper_idx_vec_ptr_t = std::shared_ptr<oper_idx_vec_t>;

class OperatorIndex {
  private:
    std::string name_;
    func_type_ptr_t type_;
    std::string uri_;

  public:
    OperatorIndex(
        const std::string &name, const func_type_ptr_t &&type, const std::string &uri = "")
        : name_(name), type_(std::move(type)), uri_(uri) {}

    const std::string &name() const { return name_; }
    const std::string &uri() const { return uri_; }
    const func_type_ptr_t &funcType() const { return type_; }
};
