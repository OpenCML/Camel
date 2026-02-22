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
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/resolver.h"
#include "executor.h"

SysModule::SysModule(context_ptr_t ctx) : BuiltinModule("sys", ctx) {
    exportEntity(
        "get_env",
        OperatorGroup::create(
            "get_env",
            {{"sys:get_env",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String())}}));
    exportEntity(
        "get_cwd",
        OperatorGroup::create(
            "get_cwd",
            {{"sys:get_cwd", StaticFuncTypeResolver::create({}, {}, Type::String())}}));
}

module_ptr_t SysModule::create(context_ptr_t ctx) { return std::make_shared<SysModule>(ctx); }

bool SysModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("sys", [ctx = context_]() { return createSysExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new SysModule(ctx->shared_from_this()); }
}
