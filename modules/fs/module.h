// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/module/builtin.h"

class FsModule : public camel::core::module::BuiltinModule {
  public:
    FsModule(camel::core::context::context_ptr_t ctx);
    ~FsModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
