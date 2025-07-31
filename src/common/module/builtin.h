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
 * Created: Jul. 29, 2025
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

class BuiltinModule : public Module {
  public:
    BuiltinModule(const std::string &name) : Module(name, "") {}
    virtual ~BuiltinModule() = default;
};

std::optional<module_ptr_t> getBuiltinModule(const std::string &name);

inline operator_ptr_t makeOperator(const std::string &name, const func_type_ptr_t &&type, OperatorFunction &&func) {
    return std::make_shared<Operator>(name, std::move(type), std::move(func));
}

inline func_type_ptr_t makeFuncType(const param_init_list &with, const param_init_list &norm,
                                    const type_ptr_t &returnType) {
    return std::make_shared<FunctionType>(with, norm, returnType);
}