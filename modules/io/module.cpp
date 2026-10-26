// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The terminal module: `io` is the terminal until `Stream` exists. Its operators are
 * the readers of stdin and the writers of the terminal's mode and buffer; the standard streams and
 * `print`/`println`/`input`/`eprintln` are the prelude's.
 *
 * The signatures are written once, in the declaration module `stdlib/io.cml` (design principles
 * 6.4). What is left here is what only C++ knows: the kernels in
 * operators.cpp, the executor that runs them, and the operator traits declared at load.
 */

#include "module.h"
#include "camel/core/context/context.h"
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
        });
    // What each touches (docs/design/op-semantics 8): raw mode writes the terminal, the readers
    // read or read-modify-write stdin, so that they stay ordered against `:io/input`.
    //
    // All five operators name the one `stdin` resource (design principles 3.3: terminal mode
    // and reading input must order each other, so raw mode is not a resource of its own).
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "io",
            {
                {"set_terminal_raw_mode", externalAmbient("stdin", AccessMode::Write)},
                {"has_input", externalAmbient("stdin", AccessMode::Read)},
                {"get_chars", externalAmbient("stdin", AccessMode::ReadModifyWrite)},
                {"clear_input_buffer", externalAmbient("stdin", AccessMode::ReadModifyWrite)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new IOModule(ctx->shared_from_this()); }
}