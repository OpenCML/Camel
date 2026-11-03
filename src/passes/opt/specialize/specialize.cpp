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
    return applyOptimizeRewritePass(context_, program, os, config);
}
