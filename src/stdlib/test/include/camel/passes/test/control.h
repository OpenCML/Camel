// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only topology checks for bounded control dependency analysis. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kControlTestPass = "test::control";
class ControlTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
