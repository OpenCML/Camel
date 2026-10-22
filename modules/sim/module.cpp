// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"

#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

SimModule::SimModule(context_ptr_t ctx) : BuiltinModule("sim", ctx) {
    // Both kernels are functions of their arguments: the random generator is local and seeded
    // from the seed argument. Registered pure so that schedulers may run them concurrently
    // (std::taskflow keeps the elements of a map in order unless the element function is
    // provably free of effects) and passes may fold or merge them.
    for (const char *uri : {"sim:pi_hits", "sim:asian_call_price"}) {
        camel::core::OperatorTable::instance().set(uri, {.pure = true, .elementwise = false});
    }
    // Both signatures are fixed and are declared in stdlib/sim.cml (handover H179); the C++ side
    // keeps the kernels and the executor, and exports no operator group of its own.
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
