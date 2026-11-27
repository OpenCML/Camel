// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of who owns an operator's type rule: two contexts alive at once each answer
 * `to_range` with their own rule, whose signature names their own `Range`. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kResolverOwnerTestPass = "test::resolver_owner";
class ResolverOwnerTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};