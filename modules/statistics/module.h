// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/module/builtin.h"

class StatisticsModule : public camel::core::module::BuiltinModule {
  public:
    StatisticsModule(camel::core::context::context_ptr_t ctx);
    ~StatisticsModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
