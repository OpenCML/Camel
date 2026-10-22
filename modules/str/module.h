// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/module/builtin.h"

class StrModule : public camel::core::module::BuiltinModule {
  public:
    StrModule(camel::core::context::context_ptr_t ctx);
    ~StrModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
