// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "specialize.h"

#include "../inline/engine.h"

const camel::runtime::Program *
SpecializeRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    OptimizeRewriteConfig config{};
    config.growthBudget           = growthBudget_;
    config.enableDevirtualization = false;
    config.enableSpecialization   = true;
    config.enableInlining         = false;
    // The engine counts the runs that changed one graph; the count is this pass's, so it lives as
    // long as the pass and no longer (engine.cpp requires it).
    config.settleStreak = &settleStreak_;
    // Its growth budget is this pass's too: `stdlib/std.cml` declares `growth_budget` on the pass,
    // so the count a run spends is one the pass keeps, cleared here where the run starts.
    growthSpent_            = 0;
    config.growthSpentNodes = &growthSpent_;
    return applyOptimizeRewritePass(context_, program, os, config);
}
