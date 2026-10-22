// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check of the equivalence classes of values with several writings. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kWritingsTestPass = "test::writings";
inline constexpr const char *kTabledTestPass   = "test::tabled";
class WritingsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};

/* Leaves the program in the state with several writings: `a + 1` of the function `f` is also written `1 + a`. */
class TabledTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
