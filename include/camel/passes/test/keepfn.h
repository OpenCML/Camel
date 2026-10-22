// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check that a function value of the heap keeps its Program through a collection. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kKeepFnTestPass = "test::keepfn";
class KeepFnTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
