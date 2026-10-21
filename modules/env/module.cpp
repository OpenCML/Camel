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
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/resolver.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

EnvModule::EnvModule(context_ptr_t ctx) : BuiltinModule("env", ctx) {
    exportEntity(
        "get_env",
        OperatorGroup::create(
            "get_env",
            {{"env:get_env",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String())}}));
    exportEntity(
        "get_cwd",
        OperatorGroup::create(
            "get_cwd",
            {{"env:get_cwd", StaticFuncTypeResolver::create({}, {}, Type::String())}}));
}

module_ptr_t EnvModule::create(context_ptr_t ctx) { return std::make_shared<EnvModule>(ctx); }

bool EnvModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("env", [ctx = context_]() { return createEnvExecutor(ctx); });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "env",
        {
            // The environment of the process.
            {"get_env", false},
            {"get_cwd", false},
        });
    // What each touches (docs/design/op-semantics 8): the environment of the process.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "env",
            {
                {"get_env", externalAmbient("process:environment", AccessMode::Read)},
                {"get_cwd", externalAmbient("process:environment", AccessMode::Read)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new EnvModule(ctx->shared_from_this()); }
}
