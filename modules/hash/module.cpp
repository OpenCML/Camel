/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 */

/*
 * SHA-256 of text and of binary values. `sha256` is two operators (`hash:sha256_str` and
 * `hash:sha256_bytes`), chosen by the argument type; `sha256_rounds` applies the digest to its own
 * hexadecimal text a number of times.
 *
 * The signatures are written once, in the declaration module `stdlib/hash.cml` (design principles
 * 6.4; handover H19, step 10; H163). What is left here is what only C++ knows: the kernels in
 * operators.cpp, the executor that runs them, and the purity declared at load. The three
 * operators are pure: they read no clock, no file and no generator.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

HashModule::HashModule(context_ptr_t ctx) : BuiltinModule("hash", ctx) {}

module_ptr_t HashModule::create(context_ptr_t ctx) { return std::make_shared<HashModule>(ctx); }

bool HashModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("hash", [ctx = context_]() {
        return createHashExecutor(ctx);
    });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "hash",
        {
            {"sha256_str", true},
            {"sha256_bytes", true},
            {"sha256_rounds", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new HashModule(ctx->shared_from_this()); }
}
