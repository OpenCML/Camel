// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/module/builtin.h"
class RandomModule : public camel::core::module::BuiltinModule {
  public:
    RandomModule(camel::core::context::context_ptr_t ctx);
    ~RandomModule() override = default;
    bool load() override;
    /// The operators of this module are named `rand:...`, not `random:...`, so the declaration
    /// module `stdlib/random.cml` may name them with a `#op` tag.
    std::vector<std::string> operatorProtocols() const override { return {"rand"}; }
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
