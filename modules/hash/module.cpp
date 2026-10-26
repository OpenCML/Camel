// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * SHA-256 of a binary value: the one operator `hash:sha256_bytes`. `sha256` of a text is written in
 * Camel in `stdlib/hash.cml`, as the digest of the text's UTF-8 bytes.
 *
 * The signatures are written once, in the declaration module `stdlib/hash.cml` (design principles
 * 6.4). What is left here is what only C++ knows: the kernels in
 * operators.cpp, the executor that runs them, and the purity declared at load. The
 * operator is pure: they read no clock, no file and no generator.
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
            {"sha256_bytes", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new HashModule(ctx->shared_from_this()); }
}
