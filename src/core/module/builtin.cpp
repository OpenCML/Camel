// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/module/builtin.h"
#include "camel/core/data/composite/func.h"
#include "camel/core/operator.h"

#include "builtin/modules/globals.h"
#include "builtin/modules/profiler.h"
#include "builtin/modules/test_attrs.h"

using namespace camel::core::context;

namespace camel::core::module {

std::unordered_map<std::string, std::function<std::shared_ptr<Module>(context_ptr_t ctx)>>
    builtinModuleFactories = {
        {"", [](context_ptr_t ctx) { return GlobalsBuiltinModule::create(ctx); }},
        // Exports its decorators always; no environment variable gates them (test_attrs.h).
        {"test_attrs", [](context_ptr_t ctx) { return TestAttrsBuiltinModule::create(ctx); }},

#ifndef NDEBUG
        {"profiler", [](context_ptr_t ctx) { return ProfilerBuiltinModule::create(ctx); }},
#endif
};

} // namespace camel::core::module
