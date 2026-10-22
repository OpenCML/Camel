// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/module/builtin.h"

class HashModule : public camel::core::module::BuiltinModule {
  public:
    HashModule(camel::core::context::context_ptr_t ctx);
    ~HashModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
