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
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "pi_hits",
            {{"sim:pi_hits",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::Int64(), false}, {Type::Int64(), false}},
                  Type::Int64())}}),
        OperatorGroup::create(
            "asian_call_price",
            {{"sim:asian_call_price",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::Int64(), false}, {Type::Int64(), false}, {Type::Int64(), false}},
                  Type::Float64())}}),
    };
    return groups;
}
} // namespace

SimModule::SimModule(context_ptr_t ctx) : BuiltinModule("sim", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t SimModule::create(context_ptr_t ctx) { return std::make_shared<SimModule>(ctx); }

bool SimModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("sim", [ctx = context_]() { return createSimExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new SimModule(ctx->shared_from_this()); }
}
