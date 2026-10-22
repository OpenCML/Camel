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
    return applyOptimizeRewritePass(context_, program, os, config);
}
