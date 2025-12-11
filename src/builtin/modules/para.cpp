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
 * Created: Oct. 09, 2025
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "para.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "unordered_reduce",
            {
                {
                    ":mark/unordered_reduce_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {2, {false, false}}},
                        "<func: (acc: U, item: T) => U, initial: U> (collect: T[]) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(with[0]);
                            if (norm[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType  = tt::as_shared<FunctionType>(norm[0]);
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[1].first->equals(vecType->elemType()))
                                return nullopt;
                            if (!normTypes[0].first->equals(norm[1]))
                                return nullopt;
                            if (!funcType->exitType()->equals(norm[1]))
                                return nullopt;
                            return norm[1];
                        }),
                },
            }),
        OperatorGroup::create(
            "unordered_foreach",
            {
                {
                    ":mark/unordered_foreach_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<func: (item: T) => void> (collect: T[]) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(with[0]);
                            if (norm[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(norm[0]);
                            if (funcType->normTypes().size() != 1)
                                return nullopt;
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[0].first->equals(vecType->elemType()))
                                return nullopt;
                            if (!funcType->exitType()->equals(Type::Void()))
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
    };

    return groups;
}

ParaBuiltinModule::ParaBuiltinModule(context_ptr_t ctx) : BuiltinModule("para", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool ParaBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

module_ptr_t ParaBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<ParaBuiltinModule>(ctx);
}
