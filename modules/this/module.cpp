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
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

ThisModule::ThisModule(context_ptr_t ctx) : BuiltinModule("this", ctx) {
    exportEntity(
        "zen",
        OperatorGroup::create(
            "zen",
            {{"this:zen", StaticFuncTypeResolver::create({}, {}, Type::String())}}));
}

module_ptr_t ThisModule::create(context_ptr_t ctx) { return std::make_shared<ThisModule>(ctx); }

bool ThisModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("this", [ctx = context_]() {
        return createThisExecutor(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ThisModule(ctx->shared_from_this()); }
}
