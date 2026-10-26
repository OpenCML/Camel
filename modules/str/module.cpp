// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The string library (design principles 11.1: a string is valid UTF-8, its length and
 * indices count code points; `bytes::encode`/`bytes::decode` cross to the UTF-8 bytes).
 *
 * Every operator is a primitive written in C++ and is pure. The string an operator works on is
 * its one ordinary argument and everything else is a `with` argument, as for the built-in
 * `join<sep>(xs)` and `slice<start, end>(xs)`, so calls chain: `s->trim->split<','>`.
 * DESIGN-QUESTION(param-style): the alternative is ordinary arguments, `split(s, ',')`.
 *
 * The signatures are written once, in the declaration module `stdlib/str.cml` (design
 * principles 6.4). What is left here is what only C++ knows: the kernels
 * in operators.cpp, the executor that runs them, and the operator traits declared at load.
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
         {"trim", true},
         {"trim_start", true},
         {"trim_end", true},
         {"to_upper", true},
         {"to_lower", true},
         {"replace", true},
         {"find", true},
         {"substr", true},
         {"starts_with", true},
         {"ends_with", true},
         {"repeat", true},
         {"codepoints", true},
         {"from_codepoints", true}});
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new StrModule(ctx->shared_from_this()); }
}
