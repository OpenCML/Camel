// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: prints the references the execution plan says come back, per position, so that a table
 * nothing executes yet is still a reading someone can inherit (camel/runtime/exec_plan.h, "The
 * references that come back"). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kPlanRefsTestPass = "test::plan_refs";
class PlanRefsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};