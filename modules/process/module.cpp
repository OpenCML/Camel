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
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "operators.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "sleep",
            {{"process:sleep",
              StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void())}}),
        OperatorGroup::create(
            "whoami",
            {{"process:whoami", StaticFuncTypeResolver::create({}, {}, Type::String())}}),
        OperatorGroup::create(
            "cpu_count",
            {{"process:cpu_count", StaticFuncTypeResolver::create({}, {}, Type::Int64())}}),
    };
    return groups;
}
} // namespace

ProcessModule::ProcessModule(context_ptr_t ctx) : BuiltinModule("process", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t ProcessModule::create(context_ptr_t ctx) { return std::make_shared<ProcessModule>(ctx); }

bool ProcessModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("process", [ctx]() { return createProcessExecutor(ctx); });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "process",
        {
            // The process and the machine it runs on. The terminal is `io`'s (H19 step 4).
            {"exit", false},
            {"sleep", false},
            {"whoami", false},
            {"cpu_count", false},
        });
    // What each touches (docs/design/op-semantics 8): the process and the machine.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "process",
            {
                {"exit", externalAmbient("process", AccessMode::ReadModifyWrite)},
                {"sleep", externalAmbient("time:clock", AccessMode::Read)},
                {"whoami", externalAmbient("process:environment", AccessMode::Read)},
                {"cpu_count", externalAmbient("process:environment", AccessMode::Read)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ProcessModule(ctx->shared_from_this()); }
}
