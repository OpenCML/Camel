// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "opt.h"

#include "camel/execute/pass/base.h"
#include "camel/passes/opt/generic.h"

#include <format>

const camel::runtime::Program *
OptimizeRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return simplifyGraph(context_, program, config_, os, 16, this);
}

const camel::runtime::Program *
OptimizeSaturatePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The laws live once, in stdlib/rules.cml (as they do for std::opt::algebra): this pass is
    // the library's saturation without its extraction, so a caller sees the state with several
    // writings. The two parameters are the library pass's own (design principles 9.3).
    const PassRunResult result = applyRegisteredPass(
        program,
        std::format(
            "rules::saturate_only rounds={} strict={}", rounds_, strict_ ? "true" : "false"),
        context_,
        os,
        this);
    return result.program;
}