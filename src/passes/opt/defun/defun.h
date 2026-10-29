// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"

/// std::defunctionalize: replaces higher-order values by what they are, so that what is left of
/// them is a direct call (see defun.cpp for the form and for what it does not lower).
class DefunctionalizeRewritePass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    ~DefunctionalizeRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};