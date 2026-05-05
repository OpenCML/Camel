/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"

#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "executor.h"
#include "operators.h"
#include "type.h"
#include "vjp_registry.h"

using namespace camel::core::context;
using namespace camel::core::module;

NnModule::NnModule(context_ptr_t ctx) : BuiltinModule("nn", ctx) {
    exportType(Reference("Parameter"), camel::nn::ParameterType::Default());
    for (const auto &group : getNnOperatorGroups()) {
        exportEntity(group->name(), group);
        if (group->name() == "parameter") {
            exportEntity(Reference(std::vector<std::string>{"Parameter"}, "new"), group);
        } else if (group->name() == "value" || group->name() == "grad") {
            exportEntity(Reference(std::vector<std::string>{"Parameter"}, group->name()), group);
        }
    }
}

module_ptr_t NnModule::create(context_ptr_t ctx) { return std::make_shared<NnModule>(ctx); }

bool NnModule::load() {
    if (loaded_) {
        return true;
    }
    camel::nn::ensureBuiltinVjpRulesRegistered();
    context_->registerExecutorFactory("nn", [ctx = context_]() { return createNnExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new NnModule(ctx->shared_from_this()); }
}
