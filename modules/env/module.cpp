// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The environment of the process: the value of a variable and the current working directory.
 *
 * The signatures are written once, in the declaration module `stdlib/env.cml` (design principles
 * 6.4). What is left here is what only C++ knows: the kernels in
 * operators.cpp, the executor that runs them, and the operator traits declared at load -- the
 * purity and the `env` resource both names read.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

EnvModule::EnvModule(context_ptr_t ctx) : BuiltinModule("env", ctx) {}

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
                {"get_env", externalAmbient("env", AccessMode::Read)},
                {"get_cwd", externalAmbient("env", AccessMode::Read)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new EnvModule(ctx->shared_from_this()); }
}
