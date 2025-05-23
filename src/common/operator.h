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

#include "data.h"

#include <functional>
#include <unordered_map>

class Context;

using OperatorFunction = std::function<data_ptr_t(Context &, data_vec_t &&)>;

class Operator {
  private:
    std::string name_;
    func_type_ptr_t type_;
    OperatorFunction func_;

  public:
    Operator(const func_type_ptr_t &&type, OperatorFunction &&func)
        : name_(type->name()), type_(std::move(type)), func_(std::move(func)) {}

    const std::string &name() const { return name_; }
    const func_type_ptr_t &type() const { return type_; }
    const OperatorFunction &func() const { return func_; }
};

using oper_ptr_t = std::shared_ptr<Operator>;
using oper_vec_t = std::vector<oper_ptr_t>;

extern std::unordered_map<std::string, std::shared_ptr<oper_vec_t>> globalOperators;

void registerOperator(const oper_ptr_t &&op);
