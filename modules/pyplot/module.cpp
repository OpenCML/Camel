// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/other.h"
#include "camel/core/type/resolver.h"
#include "modules/python/embed.h"
#include "executor.h"
#include "operators.h"

#include <exception>
#include <string>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

// The module exports no operator group of its own: `plot` is declared in `stdlib/pyplot.cml`,
// which is the one source of its signature (design principles 6.4).
// The C++ side keeps the kernel, the executor and the operator traits.
PyplotModule::PyplotModule(context_ptr_t ctx) : BuiltinModule("pyplot", ctx) {}

module_ptr_t PyplotModule::create(context_ptr_t ctx) { return std::make_shared<PyplotModule>(ctx); }

void ensurePyplotInterpreter() {
    try {
        camel::pyembed::ensureInitialized();
    } catch (const std::exception &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("Failed to initialise pyplot's embedded Python interpreter at its first "
                        "use: ") +
                e.what());
    }
}

bool PyplotModule::load() {
    if (loaded_)
        return true;
    // `import` executes no code (design principles 3.1): loading registers the executor and the purity of the operator,
    // which the declaration loader needs, and nothing that touches CPython. The interpreter is
    // created by the first `plot` (`ensurePyplotInterpreter`), or attached to when `python`
    // created it first.
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("pyplot", [ctx]() { return createPyplotExecutor(ctx); });
    // The purity of the module's operator (design principles 3.3). `plot` writes an image file
    // (or opens a window when matplotlib shows one): no pass may fold, merge or drop the call.
    // Without this entry the declaration loader refuses a body-less declaration of `pyplot:plot`
    // (`Builder::declareNativeOperator`, src/compile/gir/builder_modules.cpp#declareNativeOperator).
    camel::core::OperatorTable::instance().declare("pyplot", {{"plot", false}});
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new PyplotModule(ctx->shared_from_this()); }
}