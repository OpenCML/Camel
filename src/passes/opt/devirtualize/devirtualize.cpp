// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "devirtualize.h"

#include "../inline/engine.h"

const camel::runtime::Program *
DevirtualizeRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    OptimizeRewriteConfig config{};
    config.growthBudget           = growthBudget_;
    config.enableDevirtualization = true;
    config.enableSpecialization   = false;
    config.enableInlining         = false;
    return applyOptimizeRewritePass(context_, program, os, config);
}
