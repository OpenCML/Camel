// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../inline/config.h"
#include "camel/execute/pass/opt.h"

class SpecializeRewritePass : public RuntimeGraphRewritePass {
  public:
    explicit SpecializeRewritePass(
        const camel::core::context::context_ptr_t &ctx, const GrowthBudget &growthBudget = {})
        : RuntimeGraphRewritePass(ctx), growthBudget_(growthBudget) {}
    ~SpecializeRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    GrowthBudget growthBudget_{};
};
