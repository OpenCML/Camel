// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The native half of the string library (design principles 11.1: a string is valid UTF-8, its
 * length and indices count code points; `bytes::encode`/`bytes::decode` cross to the UTF-8 bytes).
 *
 * `stdlib/str.cml` is the module: it declares every signature and writes in Camel everything a
 * string's code points can express. What stays here is what Camel cannot say -- letter case and
 * code points as numbers -- and the kernels `split` and `index_of` carry under `#impl`, which
 * answer exactly what their Camel bodies say. Every operator is a function of its arguments.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

StrModule::StrModule(context_ptr_t ctx) : BuiltinModule("str", ctx) {}

module_ptr_t StrModule::create(context_ptr_t ctx) { return std::make_shared<StrModule>(ctx); }

bool StrModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("str", [ctx = context_]() { return createStrExecutor(ctx); });
    // Every operator of this module is a function of its arguments. The purity is a fact of the
    // implementation, so it stays here; the signatures are the declaration module's.
    camel::core::OperatorTable::instance().declare(
        "str",
        {{"split", true},
         {"to_upper", true},
         {"to_lower", true},
         {"find", true},
         {"repeat", true},
         {"codepoints", true},
         {"from_codepoints", true}});
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new StrModule(ctx->shared_from_this()); }
}
