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
    /// The runs in a row that changed one graph: `applyOptimizeRewritePass` requires it, and a run
    /// given none is refused (engine.cpp). It belongs to this pass, so two programs in one process
    /// do not share a count, and a run on another graph starts its own.
    SettleStreak settleStreak_{};
};
