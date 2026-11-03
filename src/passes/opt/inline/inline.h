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
    /// The runs in a row that changed one graph: `applyOptimizeRewritePass` requires it, and a run
    /// given none is refused (engine.cpp). It belongs to this pass, so two programs in one process
    /// do not share a count, and a run on another graph starts its own.
    SettleStreak settleStreak_{};
};
