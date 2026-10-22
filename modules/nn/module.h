// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Native nn module.
 *
 * Neural-network building blocks on top of the tensor module: convolution,
 * pooling, embedding, batch norm and loss operators with their derivative
 * rules, and optimizers over parameter trees (see optim.h). Gradients come
 * from the autodiff module.
 */

#pragma once

#include "camel/core/module/builtin.h"

class NnModule : public camel::core::module::BuiltinModule {
  public:
    explicit NnModule(camel::core::context::context_ptr_t ctx);
    ~NnModule() override = default;

    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
