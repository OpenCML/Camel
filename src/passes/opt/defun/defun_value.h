// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"

/// std::defun_value: every function the program takes as a value becomes one variant of the enum
/// that stands for its type -- the values it captured are that variant's data -- and every call
/// through such a value becomes a direct call of the function it names, with the captures as
/// trailing arguments. See defun_value.cpp for what it requires and what it refuses.
class DefunValueRewritePass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    ~DefunValueRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};