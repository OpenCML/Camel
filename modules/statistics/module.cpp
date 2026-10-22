// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
