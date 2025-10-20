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
 * Updated: Oct. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "time.h"

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "now",
            {
                {
                    ":time/now",
                    StaticFuncTypeResolver::create({}, {}, Type::Double()),
                },
            }),
        OperatorGroup::create(
            "strftime",
            {
                {
                    ":time/strftime",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::String(), false}},
                        Type::String()),
                },
            }),
        OperatorGroup::create(
            "strptime",
            {
                {
                    ":time/strptime",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Double()),
                },
            }),
    };

    return groups;
}

TimeBuiltinModule::TimeBuiltinModule(context_ptr_t ctx) : BuiltinModule("time", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t TimeBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<TimeBuiltinModule>(ctx);
}

bool TimeBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
