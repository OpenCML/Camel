// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check that what a module registered with the one process-wide derivative registry
 * survives a second `Context`: the core registers its tangent spaces on every construction, and a
 * module's subset space is registered once, so a second context must replace the core's entry and
 * leave the module's in place. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kDerivativesTestPass = "test::derivatives";
class DerivativesTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};