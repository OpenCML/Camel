// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "module.h"

namespace camel::core::module {

extern std::unordered_map<
    std::string, std::function<std::shared_ptr<Module>(camel::core::context::context_ptr_t ctx)>>
    builtinModuleFactories;

class BuiltinModule : public Module {
  public:
    BuiltinModule(const std::string &name, camel::core::context::context_ptr_t ctx)
        : Module(name, "", ctx) {}
    virtual ~BuiltinModule() = default;

    virtual bool load() = 0;
};

} // namespace camel::core::module
