// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: prints the execution plan of each graph (the order of its nodes, their regions, and
 * the entries and tails of the arms of its branches). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kExecPlanTestPass = "test::exec_plan";
class ExecPlanTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
