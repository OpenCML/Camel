// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "opt.h"

#include "camel/passes/opt/generic.h"

const camel::runtime::Program *
OptimizeRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return simplifyGraph(context_, program, config_, os);
}
