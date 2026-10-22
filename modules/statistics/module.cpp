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
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The statistics module: descriptive functions over an array of numbers. Design principles 6.4:
 * the signature is written once, in the declaration module `stdlib/statistics.cml` (handover
 * H19, step 10). What is left here is what only C++ knows: the kernels in operators.cpp, the
 * executor that runs them, and the operator trait declared at load.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "operators.h"

using namespace camel::core::context;
using namespace camel::core::module;

StatisticsModule::StatisticsModule(context_ptr_t ctx) : BuiltinModule("statistics", ctx) {}

module_ptr_t StatisticsModule::create(context_ptr_t ctx) {
    return std::make_shared<StatisticsModule>(ctx);
}

bool StatisticsModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("statistics", [ctx = context_]() {
        return createStatisticsExecutor(ctx);
    });
    // Every operator of this module is a function of its arguments (design principles 3.3). The
    // purity is a fact of the implementation, so it stays here; the signatures are the
    // declaration module's.
    for (const auto &[name, op] : getStatisticsOpsMap()) {
        camel::core::OperatorTable::instance().set(
            "statistics:" + name,
            {.pure = true, .elementwise = false});
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new StatisticsModule(ctx->shared_from_this()); }
}
