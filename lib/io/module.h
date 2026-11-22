// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/module/builtin.h"

class IOModule : public camel::core::module::BuiltinModule {
  public:
    IOModule(camel::core::context::context_ptr_t ctx);
    ~IOModule() override = default;

    bool load() override;

    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
