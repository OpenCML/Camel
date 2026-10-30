// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * transforms module wiring.
 *
 * The module exists and loads; it exports nothing yet. `reinterp<H>(f)` is written in
 * docs/design/program-transforms/README.cn.md 4.1 as
 * `func reinterp::<F, H>(f: F): Reinterp<F, H> when is_function::<F>() && is_rule_set::<H>()`,
 * and neither `Reinterp` nor `is_rule_set` exists in the core or the library, so that signature
 * cannot be written today.
 */

#include "module.h"

using namespace camel::core::context;
using namespace camel::core::module;

TransformsModule::TransformsModule(context_ptr_t ctx) : BuiltinModule("transforms", ctx) {}

module_ptr_t TransformsModule::create(context_ptr_t ctx) {
    return std::make_shared<TransformsModule>(ctx);
}

bool TransformsModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TransformsModule(ctx->shared_from_this()); }
}
