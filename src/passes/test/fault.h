// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY fault-injection passes. They are not part of any pipeline: they exist only to be
 * named by a test's pipeline, so naming one is the whole switch (removed the environment
 * variable that used to gate them).
 *
 * `test::drop_call_argument` commits a structurally broken program on purpose: it removes the
 * last argument of the first direct call (FUNC node) whose callee takes one. The regression
 * tests use it to check that the structural validation run when a rewrite is committed rejects
 * such a graph in every build mode, as an internal error naming the pass.
 *
 * `test::leak_branch_arm` and `test::share_branch_arm` commit a graph that breaks the branch-arm
 * invariant (docs/technical/17_graph_rewrite_api_contracts.md, 4.1) on purpose, in the first
 * branch of `main`: the first adds a node that waits on the BRCH and that only the JOIN waits
 * on (it belongs to no arm), the second makes the second arm wait on the first (the first arm's
 * tail then belongs to two arms). Both graphs are otherwise well formed; before the invariant
 * was checked at commit, the back ends ran them and computed something else.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kDropCallArgumentPass = "test::drop_call_argument";
inline constexpr const char *kLeakBranchArmPass    = "test::leak_branch_arm";
inline constexpr const char *kShareBranchArmPass   = "test::share_branch_arm";

class DropCallArgumentTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class LeakBranchArmTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class ShareBranchArmTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
