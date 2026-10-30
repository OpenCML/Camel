// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * transforms module: the general graph operators of program transforms (design:
 * docs/design/program-transforms/README.cn.md). `reinterp`, `split` and `transpose` handle
 * calls, branches and loops once each, and differentiation is their composition in the library.
 */

#pragma once

#include "camel/core/module/builtin.h"

class TransformsModule : public camel::core::module::BuiltinModule {
  public:
    explicit TransformsModule(camel::core::context::context_ptr_t ctx);
    ~TransformsModule() override = default;

    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
