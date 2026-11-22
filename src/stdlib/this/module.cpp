// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "operators.h"

using namespace camel::core::context;
using namespace camel::core::module;

/*
 * The Zen of Camel, as a string value. Its signature is written once, in the declaration module
 * `stdlib/this.cml` (design principles 6.4); what is left here is the
 * kernel in operators.cpp, the executor that runs it, and the purity declared at load.
 */

ThisModule::ThisModule(context_ptr_t ctx) : BuiltinModule("this", ctx) {}

module_ptr_t ThisModule::create(context_ptr_t ctx) { return std::make_shared<ThisModule>(ctx); }

bool ThisModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("this", [ctx = context_]() {
        return createThisExecutor(ctx);
    });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "this",
        {
            // A constant text.
            {"zen", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ThisModule(ctx->shared_from_this()); }
}
