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

/* Test-only: the savepoint of a draft. A rewrite that fails partway leaves the draft as it found
 * it, so running it again gives the result a run that never failed gives; and a saturation round
 * the budget cuts lands nothing, reports where it stopped, and leaves a draft a full run resumes
 * from to the same fixed point. The program is passed on unchanged. */
inline constexpr const char *kSavepointTestPass = "test::savepoint";
class SavepointTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
