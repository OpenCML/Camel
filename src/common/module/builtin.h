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

data_ptr_t __not_implemented__(Context &ctx, data_vec_t &with, data_vec_t &norm);

inline func_type_ptr_t
makeFuncType(const param_init_list &with, const param_init_list &norm, const type_ptr_t &ret) {
    return std::make_shared<FunctionType>(with, norm, ret);
}

inline oper_idx_ptr_t
makeOperator(const std::string &name, const func_type_ptr_t &&type, const std::string &uri) {
    return std::make_shared<OperatorIndex>(name, std::move(type), uri);
}

class BuiltinModule : public Module {
  public:
    BuiltinModule(const std::string &name, context_ptr_t ctx) : Module(name, "", ctx) {
        loaded_ = true;
    }
    virtual ~BuiltinModule() = default;

    virtual bool load() = 0;

    void exportBuiltinOperator(
        const std::string &name, const param_init_list &with, const param_init_list &norm,
        const type_ptr_t &ret, const std::string &uri) {
        auto op = makeOperator(name, makeFuncType(with, norm, ret), uri);
        auto ops = std::make_shared<std::vector<std::shared_ptr<OperatorIndex>>>();
        ops->push_back(op);
        exportEntity(name, ops);
    }
};

std::optional<module_ptr_t> getBuiltinModule(const std::string &name, context_ptr_t ctx);