// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * laws module wiring.
 *
 * The module is one Camel file (`src/stdlib/laws.cml`: the tangent and the cotangent of each
 * primitive) plus one native operator (`stop_gradient`, whose kernel is the identity and whose rule
 * is "no gradient crosses it"). Everything else a reader looks for in this module is Camel; this
 * file exists because an operator implementation is a kernel, and a kernel is C++.
 */

#include "module.h"

#include "operators.h"

#include "camel/core/context/context.h"
#include "camel/execute/executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

class LawsExecutor : public Executor {
  public:
    explicit LawsExecutor(context_ptr_t ctx) : Executor(std::move(ctx), camel::laws::operatorKernels()) {}
};

} // namespace

LawsModule::LawsModule(context_ptr_t ctx) : BuiltinModule("laws", ctx) {
    // The traits, the resources and the derivative rule of `stop_gradient`, published when the
    // module is created: the front end compares the declaration in `laws.cml` against them.
    camel::laws::registerOperatorMetadata();
}

module_ptr_t LawsModule::create(context_ptr_t ctx) { return std::make_shared<LawsModule>(ctx); }

bool LawsModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("laws", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<LawsExecutor>(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new LawsModule(ctx->shared_from_this()); }
}