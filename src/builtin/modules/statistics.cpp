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
 * Created: Dec. 11, 2025
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "statistics.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "mean",
            {
                {
                    ":statistics/mean",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(arr: (typeas T)[]) => double",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            TypeCode elemCode =
                                tt::as_shared<ArrayType>(norm[0])->elemType()->code();
                            if (elemCode == TypeCode::Int || elemCode == TypeCode::Long ||
                                elemCode == TypeCode::Float || elemCode == TypeCode::Double)
                                return Type::Double();
                            return nullopt;
                        }),
                },
            }),
        OperatorGroup::create(
            "stdev",
            {
                {
                    ":statistics/stdev",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(arr: (typeas T)[]) => double",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            TypeCode elemCode =
                                tt::as_shared<ArrayType>(norm[0])->elemType()->code();
                            if (elemCode == TypeCode::Int || elemCode == TypeCode::Long ||
                                elemCode == TypeCode::Float || elemCode == TypeCode::Double)
                                return Type::Double();
                            return nullopt;
                        }),
                },
            }),
    };

    return groups;
}

StatisticsBuiltinModule::StatisticsBuiltinModule(context_ptr_t ctx)
    : BuiltinModule("statistics", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t StatisticsBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<StatisticsBuiltinModule>(ctx);
}

bool StatisticsBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
