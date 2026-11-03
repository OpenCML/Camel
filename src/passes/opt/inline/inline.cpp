// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "inline.h"

#include "engine.h"

const camel::runtime::Program *
InlineRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    OptimizeRewriteConfig config{
        .enableDevirtualization = false,
        .enableSpecialization   = false,
        .enableInlining         = true,
        .growthBudget           = growthBudget_,
        .inlineConfig           = config_,
    };
    // The engine counts the runs that changed one graph; the count is this pass's, so it lives as
    // long as the pass and no longer (engine.cpp requires it).
    config.settleStreak = &settleStreak_;
    return applyOptimizeRewritePass(context_, program, os, config);
}
