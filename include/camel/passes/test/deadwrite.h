// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: the judgement `camel::runtime::deadWrite` makes, printed one line per node that writes,
 * so a plan can pin the answer on its own -- no pass, no pipeline, no effect on the program. The
 * line carries the two halves the judgement is made of (`rooted`, `read`) beside the answer, so a
 * reading that goes wrong says which half did it.
 */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kDeadWriteTestPass = "test::deadwrite";
class DeadWriteTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};