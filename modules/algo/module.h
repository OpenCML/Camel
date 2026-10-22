// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/module/builtin.h"

class AlgoModule : public camel::core::module::BuiltinModule {
  public:
    AlgoModule(camel::core::context::context_ptr_t ctx);
    ~AlgoModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
