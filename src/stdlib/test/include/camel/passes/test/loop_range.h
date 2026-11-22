// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: two passes that hand the loop tables an execution order they must refuse, and ask
 * rebuildLoopTablesForTests (buildLoopTables) what the tables of that order are.
 *
 * `test::break_loop_range` puts the wait a loop carries back where the layout left it before
 * 210b42cc9 -- after the loop's piece, between the last of the loop's nodes and the LOOP node --
 * which is the order that ran a loop table it should have stopped:
 *
 *   test::break_loop_range: graph 'main': the wait 4 and the 3 nodes it is laid out with stand at
 *   15 again, after the loop's piece and before the LOOP node 52 at 19
 *
 * `test::interleave_loop_range` gives the two loops of one graph an order that interleaves them: a
 * node of the second loop's range is moved into the middle of the first's, so the first loop's
 * range has a node of another loop in it.
 *
 * Both orders are refused by the check, which names the loop and the node standing in it. What the
 * check must not refuse is a loop whose own nodes are one range with the condition inside it: that
 * is what the layout makes when the condition reads what the body's nodes make
 * (test/plans/feat/opt/e9.plan.toml, rc03_seed56.opt).
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kBreakLoopRangeTestPass       = "test::break_loop_range";
inline constexpr const char *kInterleaveLoopRangeTestPass = "test::interleave_loop_range";

/// The wait a loop carries, put back after the loop's piece.
class BreakLoopRangeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/// A node of the second loop's range, put inside the first loop's range.
class InterleaveLoopRangeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};