// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/opt.h"

/// std::defun_value: a function value that captures nothing and names one function of the program
/// becomes a tag -- an enum value with one variant and no data -- and every call through it becomes
/// a direct call of that function (see defun_value.cpp for what it requires and what it refuses).
class DefunValueRewritePass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    ~DefunValueRewritePass() override = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};