// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: rules written as pairs of functions, compiled and saturated. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kRulesTestPass       = "test::rules";
inline constexpr const char *kRulesGreedyTestPass = "test::rules_greedy";
class RulesTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
/* The same rules applied one way, in place (DraftWritings::rewrite). */
class RulesGreedyTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
