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

JsonModule::JsonModule(context_ptr_t ctx) : BuiltinModule("json", ctx) {
    exportEntity(
        "encode",
        OperatorGroup::create(
            "encode",
            {{"json:encode",
              StaticFuncTypeResolver::create({}, {{Type::Any(), false}}, Type::String())},
             {"json:encode",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::Any(), false}, {Type::Int(), false}},
                  Type::String())}}));
    exportEntity(
        "decode",
        OperatorGroup::create(
            "decode",
            {{"json:decode",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Any())}}));
}

module_ptr_t JsonModule::create(context_ptr_t ctx) { return std::make_shared<JsonModule>(ctx); }

bool JsonModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("json", [ctx = context_]() {
        return createJsonExecutor(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new JsonModule(ctx->shared_from_this()); }
}
