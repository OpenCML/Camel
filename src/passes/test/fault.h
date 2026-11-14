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
inline constexpr const char *kOrderOnlyEffectPass  = "test::order_only_effect";
inline constexpr const char *kRecalleeArityPass    = "test::recallee_arity";
inline constexpr const char *kRecalleeTypePass     = "test::recallee_type";

/// TEST-ONLY: strips every edge an effect stands on except its order edges, so the effect is kept
/// in the graph by an order edge alone. An order edge orders two nodes that both run; it does not
/// make either of them run (docs/design/effects/edges.cn.md 1.5), so the commit must refuse the
/// graph with an internal error naming what the effect is missing. The credential read is the one
/// under test (`effectStandsOnSomething`, src/execute/pass.cpp).
class OrderOnlyEffectTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

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

/// TEST-ONLY: points a call at a function of another signature, which no source can express (the
/// front end type-checks a call against the value it names, and a transaction that retargets one
/// afterwards is the only way to build such a CALL: it is a call through a value, and the value is
/// a constant the graph holds in its static area). What it builds is a call site that no longer
/// says what the callee's type says, and the two halves of that are separate cases:
///
///  - `test::recallee_arity` points the first call whose callee is a static function at another
///    static function of the same graph that takes a different number of ordinary arguments;
///  - `test::recallee_type` points it at one that takes the same number of arguments of a
///    different type (no argument of the call is compatible with the port it lands in).
///
/// The cases use them to ask two questions at once: does the commit refuse such a CALL, and does a
/// pass that knows the callee (std::opt::devirtualize) write a direct call it cannot justify. The
/// pass writes the retarget and nothing else; whether the program is accepted is the answer.
class RecalleeArityTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class RecalleeTypeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
