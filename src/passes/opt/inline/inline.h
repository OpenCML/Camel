// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"
#include "config.h"

class InlineRewritePass : public RuntimeGraphRewritePass {
  public:
    InlineRewritePass(
        const camel::core::context::context_ptr_t &ctx, const InlineRewriteConfig &config = {},
        const GrowthBudget &growthBudget = {})
        : RuntimeGraphRewritePass(ctx), config_(config), growthBudget_(growthBudget) {};
    virtual ~InlineRewritePass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    InlineRewriteConfig config_{};
    GrowthBudget growthBudget_{};
};
