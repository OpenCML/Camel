// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * autodiff module: reverse-mode differentiation as a graph rewrite.
 */

#pragma once

#include "camel/core/module/builtin.h"

class AutodiffModule : public camel::core::module::BuiltinModule {
  public:
    explicit AutodiffModule(camel::core::context::context_ptr_t ctx);
    ~AutodiffModule() override = default;

    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
