// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The facilities a test of the runtime uses, not a program: a meeting point that orders the calls
 * waiting at it, and the count of the threads this process has. Both make a property of the
 * scheduler visible.
 *
 * Their signatures are written once, in the declaration module `stdlib/test_support.cml` (design
 * principles 6.4). What is left here is what only C++ knows: the kernels in operators.cpp, the
 * executor that runs them, and the operator traits and resources declared at load. Neither
 * operator is pure: one waits for other calls and one reads the process.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

TestSupportModule::TestSupportModule(context_ptr_t ctx) : BuiltinModule("test_support", ctx) {}

module_ptr_t TestSupportModule::create(context_ptr_t ctx) {
    return std::make_shared<TestSupportModule>(ctx);
}

bool TestSupportModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("test_support", [ctx]() {
        return createTestSupportExecutor(ctx);
    });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "test_support",
        {
            // Blocks until other calls arrive.
            {"rendezvous", false},
            // Reads the threads of this process.
            {"thread_count", false},
        });
    // What each touches (docs/design/op-semantics 8): the one meeting point of the process, and
    // the process itself. The declaration module names the same two resources.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "test_support",
            {
                {"rendezvous", externalAmbient("rendezvous_point", AccessMode::ReadModifyWrite)},
                {"thread_count", externalAmbient("process", AccessMode::Read)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) {
    return new TestSupportModule(ctx->shared_from_this());
}
}