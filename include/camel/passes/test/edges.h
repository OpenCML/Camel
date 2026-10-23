// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only check of the edge invariants a commit enforces (cycle, types along edges). */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kEdgesCycleTestPass = "test::edges_cycle";
inline constexpr const char *kEdgesTypeTestPass  = "test::edges_type";
inline constexpr const char *kEdgesOkTestPass    = "test::edges_ok";
inline constexpr const char *kCommitConflictTestPass = "test::commit_conflict";
inline constexpr const char *kCommitMergeTestPass    = "test::commit_merge";
inline constexpr const char *kEdgesOrderOkTestPass   = "test::edges_order_ok";
inline constexpr const char *kEdgesOrderPureTestPass = "test::edges_order_pure";
inline constexpr const char *kEdgesOrderScheduleTestPass = "test::edges_order_schedule";

// Each adds nodes to the root graph, commits, and prints "EDGES accepted" or
// "EDGES refused: <the commit's message>".
class EdgesCycleTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesTypeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesOkTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// Three sessions on one program: the first commits (and so replaces the context's program),
// the second wrote the same graph, the third another one. Prints what conflicts() says of each
// and what commitChecked() does.
class CommitConflictTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// Two sessions on one program, the second started before the first committed and writes a graph
// the first did not: the second's commit meets no conflict and merges into the program the first
// published instead of forking (design 4.3.1). Prints what the merged program holds.
class CommitMergeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// The order edge (docs/design/effects/edges.cn.md 1.5, 4.6). The first writes a valid one between
// two anchor nodes, commits, and prints what the graph the commit made holds; the second writes
// one whose user is a pure node, which the seal check refuses.
class EdgesOrderOkTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesOrderPureTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// What the schedulers make of an order edge: it orders two nodes that both run, and it does not
// make a node run. Two anchors are made live by giving the exit a control input of each, then one
// anchor is ordered after the other; a third anchor is ordered before the exit and stays out of
// the layout. Prints what the execution plan of the graph the commit made holds.
class EdgesOrderScheduleTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
