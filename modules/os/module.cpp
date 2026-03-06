/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "whoami",
            {{"os:whoami", StaticFuncTypeResolver::create({}, {}, Type::String())}}),
        OperatorGroup::create(
            "set_terminal_raw_mode",
            {{"os:set_terminal_raw_mode",
              StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Void())}}),
        OperatorGroup::create(
            "has_input",
            {{"os:has_input", StaticFuncTypeResolver::create({}, {}, Type::Bool())}}),
        OperatorGroup::create(
            "get_char",
            {{"os:get_char", StaticFuncTypeResolver::create({}, {}, Type::String())}}),
        OperatorGroup::create(
            "get_chars",
            {{"os:get_chars",
              StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String())}}),
        OperatorGroup::create(
            "clear_input_buffer",
            {{"os:clear_input_buffer", StaticFuncTypeResolver::create({}, {}, Type::Void())}}),
    };
    return groups;
}
} // namespace

OSModule::OSModule(context_ptr_t ctx) : BuiltinModule("os", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t OSModule::create(context_ptr_t ctx) { return std::make_shared<OSModule>(ctx); }

bool OSModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("os", [ctx]() { return createOSExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new OSModule(ctx->shared_from_this()); }
}
