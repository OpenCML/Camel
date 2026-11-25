// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: a Program answers about a graph by the graph's identity (see program_identity.cpp). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kProgramIdentityTestPass = "test::program_identity";
class ProgramIdentityTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};