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
    return applyOptimizeRewritePass(context_, program, os, config);
}
