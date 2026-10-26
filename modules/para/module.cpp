// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The unordered counterparts of `foreach` and `reduce`. Both are built-in element calls: the
 * operator of each is the `:mark/...` URI its declaration carries, and the builtin executor runs
 * it (src/builtin/executors/builtin.cpp). So the module registers no operator, has no kernel, no
 * executor and no resource of its own, and its whole content is `stdlib/para.cml`, the declaration
 * module beside it (design principles 6.4). `rendezvous`, the one operator that did have a kernel
 * here, left for `test_support`.
 */

#include "module.h"

using namespace camel::core::context;
using namespace camel::core::module;

ParaModule::ParaModule(context_ptr_t ctx) : BuiltinModule("para", ctx) {}

module_ptr_t ParaModule::create(context_ptr_t ctx) { return std::make_shared<ParaModule>(ctx); }

bool ParaModule::load() {
    if (loaded_)
        return true;
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ParaModule(ctx->shared_from_this()); }
}