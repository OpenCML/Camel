// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check that the regions of a graph survive a commit, a decode and a second commit. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kRegionsTestPass = "test::regions";
class RegionsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
