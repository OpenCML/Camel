// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The clock and its text forms. `now` reads the system clock, `strftime` writes a timestamp as
 * text in UTC, and `strptime` turns text into a timestamp using the process's time zone.
 *
 * The signatures are written once, in the declaration module `stdlib/time.cml` (design principles
 * 6.4). What is left here is what only C++ knows: the kernels in
 * operators.cpp, the executor that runs them, and the operator traits declared at load.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

TimeModule::TimeModule(context_ptr_t ctx) : BuiltinModule("time", ctx) {}

module_ptr_t TimeModule::create(context_ptr_t ctx) { return std::make_shared<TimeModule>(ctx); }

bool TimeModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("time", [ctx]() { return createTimeExecutor(ctx); });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "time",
        {
            // The clock.
            {"now", false},
            // strptime reads the time zone of the process. strftime formats in UTC and could
            // be declared pure; it is kept with the rest of the module until that is decided.
            {"strftime", false},
            {"strptime", false},
        });
    // What each touches (docs/design/op-semantics 8): the clock; the time zone of the process.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "time",
            {
                {"now", externalAmbient("clock", AccessMode::Read)},
                {"strftime", externalAmbient("env", AccessMode::Read)},
                {"strptime", externalAmbient("env", AccessMode::Read)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TimeModule(ctx->shared_from_this()); }
}
