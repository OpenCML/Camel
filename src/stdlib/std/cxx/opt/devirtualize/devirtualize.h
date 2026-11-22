// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../inline/config.h"
#include "camel/execute/pass/opt.h"

class DevirtualizeRewritePass : public RuntimeGraphRewritePass {
  public:
    explicit DevirtualizeRewritePass(
        const camel::core::context::context_ptr_t &ctx, const GrowthBudget &growthBudget = {})
        : RuntimeGraphRewritePass(ctx), growthBudget_(growthBudget) {}
    ~DevirtualizeRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    GrowthBudget growthBudget_{};
    /// The runs in a row that changed one graph: `applyOptimizeRewritePass` requires it, and a run
    /// given none is refused (engine.cpp). It belongs to this pass, so two programs in one process
    /// do not share a count, and a run on another graph starts its own.
    SettleStreak settleStreak_{};
    /// The nodes this pass has copied against `growth_budget`: required by
    /// `applyOptimizeRewritePass` the same way, and cleared where a run starts, so each run spends
    /// the budget `stdlib/std.cml` declares on this pass and no run starts on what one before it
    /// left. The engine used to make its own count here, which dropped the budget at every call.
    size_t growthSpent_{};
};
