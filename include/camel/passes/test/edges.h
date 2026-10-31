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
inline constexpr const char *kEdgesOrderSpliceTestPass   = "test::edges_order_splice";
inline constexpr const char *kEdgesOrderReplaceTestPass  = "test::edges_order_replace";
inline constexpr const char *kEdgesSyncGateTestPass      = "test::edges_sync_gate";
inline constexpr const char *kEdgesSyncForkTestPass      = "test::edges_sync_fork";
inline constexpr const char *kEdgesSyncForkGateTestPass  = "test::edges_sync_fork_gate";
inline constexpr const char *kEdgesSyncCycleTestPass     = "test::edges_sync_cycle";
inline constexpr const char *kEdgeUsersTestPass          = "test::edge_users";

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

// The orderings that pass through a node the graph loses (docs/design/effects/edges.cn.md 4):
// removing a node keeps A -> C of A -> B -> C, and replacing one hands A -> B and B -> C to the
// replacer as A -> D and D -> C. Both passes first write an anchor chain A -> B -> C, then remove
// or replace B through the same draft calls a pass makes, and print the order edges of the graph
// the commit produced.
class EdgesOrderSpliceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesOrderReplaceTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// The GATE of a `sync` block (docs/design/effects/edges.cn.md 5.3): one data port reading a value
// and one **sync predecessor**, which is the chain's tail, and no control input at all. The pass
// writes that shape, commits it, and prints what the graph the commit made holds: the commit must
// accept it, and it must keep the GATE rather than dissolve it into its input.
class EdgesSyncGateTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// The other half of the same rule (docs/design/effects/edges.cn.md 5.3): a block's statements are
// one chain, so one statement follows one other at most *and* one statement follows it. An inline
// splice that cannot put a statement in its place leaves two statements following one -- a chain
// that forks, which is no chain -- and no such graph may be frozen: the seal check names the
// statement the two follow. The first pass writes that fork and is refused; the second writes the
// case 5.3 allows -- one chain end handing its value on, which is a GATE per block that ends on it
// -- and is accepted, so the check is known to tell the two apart.
class EdgesSyncForkTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesSyncForkGateTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// A cycle on sync edges: two statements that each follow the other. The draft's check and the
// freeze's walk are one function over one list of edge kinds (camel/runtime/cycle.h), and a sync
// edge is a real ordering, so this shape is a cycle both of them must name -- the pass prints what
// the draft's side reports and then what the commit does with it. A freeze that walked four kinds
// again would accept this graph, which is what this case is for.
class EdgesSyncCycleTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// The users of a node, read on both sides of the freeze. A draft keeps one list per edge kind as a
// pass writes the edge; a frozen graph keeps one table built from its input runs, with the two
// side arrays beside them because neither a sync nor an order edge is a port. A pass asking what
// reads, waits on or is ordered by a node (camel/runtime/effect_liveness.h; `isUnanchoredPure`
// reads all six kinds) must get the same answer from either side, so this pass builds one node
// read in all five kinds, commits, and prints the five sets of that node and how many nodes of the
// graph the two sides disagree about. The sets are compared in the runtime refs the commit gives
// the draft's nodes: a draft's ids stop meaning anything once it is encoded.
class EdgeUsersTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
