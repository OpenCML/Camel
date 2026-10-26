// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The regular expression module. A string counts code points (design principles 11.1), so `.` and
 * a character class stand for one code point: text above ASCII is matched as wide characters.
 *
 * The signature is written once, in the declaration module `stdlib/re.cml` (design principles 6.4).
 * What is left here is what only C++ knows: the kernel in operators.cpp,
 * the executor that runs it, and the operator trait declared at load.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

ReModule::ReModule(context_ptr_t ctx) : BuiltinModule("re", ctx) {}

module_ptr_t ReModule::create(context_ptr_t ctx) { return std::make_shared<ReModule>(ctx); }

bool ReModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("re", [ctx = context_]() { return createReExecutor(ctx); });
    // A pattern is matched against its arguments, so the operator is pure (design principles 3.3).
    // The purity is a fact of the implementation, so it stays here; the signature is the
    // declaration module's.
    camel::core::OperatorTable::instance().declare(
        "re",
        {
            {"test", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ReModule(ctx->shared_from_this()); }
}
