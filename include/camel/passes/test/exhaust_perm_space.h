// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only pass that uses up the permanent space (a limit given by CAMEL_PERM_SPACE_LIMIT_MB). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kExhaustPermSpaceTestPass = "test::exhaust_perm_space";
class ExhaustPermSpaceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
