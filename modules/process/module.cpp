// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The process and the machine it runs on: how long to wait, who the user is and how many hardware
 * threads there are. `exit` is the built-in operator of `src/builtin/modules/globals.cpp`, so it
 * is not a group of this module; the terminal is `io`'s (H19 step 4).
 *
 * The signatures are written once, in the declaration module `stdlib/process.cml` (design
 * principles 6.4; handover H19, step 10; H163). What is left here is what only C++ knows: the
 * kernels in operators.cpp, the executor that runs them, and the operator traits declared at
 * load -- the purity and the resources (`clock`, `env`, `process`).
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

ProcessModule::ProcessModule(context_ptr_t ctx) : BuiltinModule("process", ctx) {}

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
                {"sleep", externalAmbient("clock", AccessMode::Read)},
                {"whoami", externalAmbient("env", AccessMode::Read)},
                {"cpu_count", externalAmbient("env", AccessMode::Read)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ProcessModule(ctx->shared_from_this()); }
}
