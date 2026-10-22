// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/module/builtin.h"
class TimeModule : public camel::core::module::BuiltinModule {
  public:
    TimeModule(camel::core::context::context_ptr_t ctx);
    ~TimeModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
