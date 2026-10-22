// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only producers of the graph-level collect capability (camel/runtime/loop.h, "Collection
 * slots"): no source syntax declares collection slots, so the tests give a loop two of them and
 * observe the tapes and the state afterwards.
 *
 * Both passes rewrite every LOOP node that has no collection slot yet: two slots, the first
 * collecting the next value of the first state, the second the same value plus one (an int
 * state; otherwise the same value twice). The body graph's exit becomes
 * (state…, slot0, slot1) and the LOOP node's value (state…, array0, array1). The loop's own
 * users see that value:
 *   test::loop_collect          the value itself: the program prints (state, tape, tape)
 *   test::loop_collect_summary  (state, len(tape0), len(tape1)), so a loop of a hundred thousand
 *                               rounds prints three numbers instead of the tapes
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kLoopCollectTestPass        = "test::loop_collect";
inline constexpr const char *kLoopCollectSummaryTestPass = "test::loop_collect_summary";

class LoopCollectTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class LoopCollectSummaryTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};