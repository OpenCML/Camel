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

#include "os.h"

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "whoami",
            {
                {
                    ":os/whoami",
                    StaticFuncTypeResolver::create({}, {}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "set_terminal_raw_mode",
            {
                {
                    ":os/set_terminal_raw_mode",
                    StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "has_input",
            {
                {
                    ":os/has_input",
                    StaticFuncTypeResolver::create({}, {}, Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "get_char",
            {
                {
                    ":os/get_char",
                    StaticFuncTypeResolver::create({}, {}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "get_chars",
            {
                {
                    ":os/get_chars",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "clear_input_buffer",
            {
                {
                    ":os/clear_input_buffer",
                    StaticFuncTypeResolver::create({}, {}, Type::Void()),
                },
            }),
    };

    return groups;
}

OSBuiltinModule::OSBuiltinModule(context_ptr_t ctx) : BuiltinModule("os", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t OSBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<OSBuiltinModule>(ctx);
}

bool OSBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
