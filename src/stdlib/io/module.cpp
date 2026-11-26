// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The terminal module: `io` is the terminal until `Stream` exists. Its operators are
 * the readers of stdin and the writers of the terminal's mode and buffer; the standard streams and
 * `print`/`println`/`eprintln` are the prelude's.
 *
 * The signatures are written once, in the declaration module `stdlib/io.cml` (design principles
 * 6.4). What is left here is what only C++ knows: the kernels in
 * operators.cpp, the executor that runs them, and the operator traits declared at load.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

IOModule::IOModule(context_ptr_t ctx) : BuiltinModule("io", ctx) {}

module_ptr_t IOModule::create(context_ptr_t ctx) { return std::make_shared<IOModule>(ctx); }

bool IOModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("io", [ctx]() { return createIOExecutor(ctx); });
    // The terminal is not a function of its arguments: every operator is effectful (design
    // principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "io",
        {
            {"set_terminal_raw_mode", false},
            {"has_input", false},
            {"get_chars", false},
            {"clear_input_buffer", false},
            {"input", false},
        });
    // `input` writes its prompt and reads a line, both on the one console resource: the terminal's
    // channels are one resource (design principles 3.3), and the order is the written one.
    const auto inputResources = [] {
        auto spec = camel::core::sem::externalAmbient(
            "console",
            camel::core::sem::AccessMode::ReadModifyWrite);
        const auto out =
            camel::core::sem::externalAmbient("console", camel::core::sem::AccessMode::Write);
        spec.accesses.insert(spec.accesses.end(), out.accesses.begin(), out.accesses.end());
        return spec;
    };
    // What each touches (docs/design/op-semantics 8): raw mode writes the terminal, the readers
    // read or read-modify-write it, so that they stay ordered against `input`.
    //
    // All five operators name the one `console` resource (design principles 3.3: the terminal's
    // channels are one resource, so terminal mode, reading input and printing order each other).
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "io",
            {
                {"set_terminal_raw_mode", externalAmbient("console", AccessMode::Write)},
                {"has_input", externalAmbient("console", AccessMode::Read)},
                {"get_chars", externalAmbient("console", AccessMode::ReadModifyWrite)},
                {"clear_input_buffer", externalAmbient("console", AccessMode::ReadModifyWrite)},
                {"input", inputResources()},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new IOModule(ctx->shared_from_this()); }
}