// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Commits a graph that breaks an edge invariant (a cycle of control inputs; a COPY whose type is
 * not its input's) and prints whether the commit accepted it. The third pass commits a valid
 * edit as the control.
 */

#include "camel/passes/test/edges.h"

#include "camel/passes/test/anchor.h"
#include "camel/core/rtdata/base.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/exec_plan.h"

#include <exception>

using camel::passes::test::anchorNode;
using camel::passes::test::blankNode;

namespace {

using namespace camel::runtime;

enum class Edit { Cycle, Type, Ok };

const Program *commitEdit(
    const camel::core::context::context_ptr_t &context, const Program *program, std::ostream &os,
    Edit edit) {
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program);
    auto &draft = session.edit(program->root()).edit();
    switch (edit) {
    case Edit::Cycle: {
        const gc_node_ref_t a = blankNode(draft);
        const gc_node_ref_t b = blankNode(draft);
        draft.setCtrlInputs(a, std::span<const gc_node_ref_t>(&b, 1));
        draft.setCtrlInputs(b, std::span<const gc_node_ref_t>(&a, 1));
        break;
    }
    case Edit::Type: {
        const gc_node_ref_t value = draft.materializeStaticValue(
            camel::core::rtdata::toSlot<float>(1.0f),
            camel::core::type::Type::Float32());
        const gc_node_ref_t copy = draft.addCopyNode(camel::core::type::Type::Int32());
        draft.setNormInputs(copy, std::span<const gc_node_ref_t>(&value, 1));
        break;
    }
    case Edit::Ok: {
        // The session hands out one graph per graph it edits, and it stays where the session put
        // it: a pass binds that graph by reference and keeps it while it edits. An accessor that
        // made a new object per call would leave the pass writing into a graph the commit never
        // sees, and a value where a reference is expected is not a compile error at the call site.
        // The identity is what can be looked at here; the type is held by the static_assert in
        // draft_session.h. Nothing here names the graph's type: what the session hands out is
        // what a pass edits, and the test is about the one it hands out, not about its name.
        auto *first     = &session.edit(program->root());
        auto *second    = &session.edit(program->root());
        blankNode(first->edit());
        auto *afterEdit = &session.edit(program->root());
        if (first == second && first == afterEdit) {
            os << "EDGES transaction stable\n";
        } else {
            os << "EDGES transaction moved: " << first << " " << second << " " << afterEdit << "\n";
        }
        blankNode(first->edit());
        break;
    }
    }
    try {
        const Program *next = session.commit();
        os << "EDGES accepted\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

} // namespace

const camel::runtime::Program *
EdgesCycleTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return commitEdit(context_, program, os, Edit::Cycle);
}

const camel::runtime::Program *
EdgesTypeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return commitEdit(context_, program, os, Edit::Type);
}

const camel::runtime::Program *
EdgesOkTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return commitEdit(context_, program, os, Edit::Ok);
}

// The order edge the design adds (docs/design/effects/edges.cn.md 1.5): it is not a port and not
// a control input, it survives the commit as an edge of the frozen graph, and the seal check
// refuses one whose user is a pure node (4.6).
const camel::runtime::Program *
EdgesOrderOkTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    const GraphId rootId = program->root()->id();
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(program->root()).edit();
    const gc_node_ref_t first  = anchorNode(draft);
    const gc_node_ref_t second = anchorNode(draft);
    os << "ORDER wrote=" << (draft.appendOrderDependency(second, first) ? 1 : 0)
       << " twice=" << (draft.appendOrderDependency(second, first) ? 1 : 0) << "\n";
    os << "ORDER preds=" << draft.orderInputsOf(second).size()
       << " users=" << draft.orderUsersOf(first).size() << "\n";
    try {
        const Program *next = session.commit();
        const GCGraph *root = next->graph(rootId);
        os << "EDGES accepted\n";
        // The graph holds the edge in its own array, and the per-node reader (the one every
        // scheduler and pass uses) agrees with that array node for node.
        size_t seen = 0;
        size_t ordered = 0;
        for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
            const auto preds = root->orderPredsOf(it.ref());
            seen += preds.size();
            ordered += preds.empty() ? 0 : 1;
        }
        os << "ORDER frozen_agrees=" << (seen == root->orderEdges().size() ? 1 : 0)
           << " some_node_ordered=" << (ordered != 0 ? 1 : 0) << "\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

const camel::runtime::Program *
EdgesOrderPureTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(program->root()).edit();
    const gc_node_ref_t anchor = anchorNode(draft);
    // A constant is a pure node: nothing about it can be ordered.
    const gc_node_ref_t constant = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(1.0f),
        camel::core::type::Type::Float32());
    os << "ORDER wrote=" << (draft.appendOrderDependency(constant, anchor) ? 1 : 0) << "\n";
    try {
        session.commit();
        os << "EDGES accepted\n";
        return program;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

const camel::runtime::Program *
EdgesOrderScheduleTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    const GraphId rootId = program->root()->id();
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(program->root()).edit();
    const gc_node_ref_t exit = draft.exitNode();
    if (exit == kInvalidNodeRef) {
        os << "ORDER no_exit=1\n";
        return program;
    }
    // Three anchors: two the exit waits for, so both run and neither reaches the other, and one
    // nothing waits for, so it does not run at all.
    const gc_node_ref_t first  = anchorNode(draft);
    const gc_node_ref_t second = anchorNode(draft);
    const gc_node_ref_t never  = anchorNode(draft);
    const gc_node_ref_t late   = anchorNode(draft);
    draft.appendInput(DraftEdgeKind::Ctrl, exit, first);
    draft.appendInput(DraftEdgeKind::Ctrl, exit, second);
    draft.appendInput(DraftEdgeKind::Ctrl, exit, late);
    // `second` orders `first` after it: the layout must put `second` first even though the exit
    // lists `first` first. `never` orders the exit after itself and must not enter the layout.
    const bool wroteOrdered = draft.appendOrderDependency(first, second);
    const bool wroteNever   = draft.appendOrderDependency(exit, never);
    os << "ORDER wrote=" << (wroteOrdered ? 1 : 0) << (wroteNever ? 1 : 0) << "\n";
    try {
        const Program *next = session.commit();
        const GCGraph *root = next->graph(rootId);
        os << "EDGES accepted\n";
        const auto &topo    = root->execPlan().topo;
        const auto position = [&](gc_node_ref_t ref) {
            const auto found = std::ranges::find(topo, ref);
            return found == topo.end() ? topo.size() : static_cast<size_t>(found - topo.begin());
        };
        // The single order edge between two nodes the layout holds: did the order move `first`
        // behind `second`? (It is listed as the exit's first control input, so without the order
        // edge it comes first.)
        size_t orderedPairs = 0;
        size_t orderedOk    = 0;
        size_t droppedAbsent = 0;
        for (const GCGraphOrderEdge &edge : root->orderEdges()) {
            const bool userRuns = position(edge.user) != topo.size();
            const bool predRuns = position(edge.pred) != topo.size();
            if (userRuns && predRuns) {
                ++orderedPairs;
                orderedOk += position(edge.pred) < position(edge.user) ? 1 : 0;
            } else if (userRuns && !predRuns) {
                ++droppedAbsent;
            }
        }
        os << "ORDER ordered_pairs=" << orderedPairs << " obeyed=" << orderedOk << "\n";
        os << "ORDER unexecuted_pred_absent=" << droppedAbsent << "\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

namespace {

using namespace camel::runtime;

/// An anchor chain A -> B -> C of three nodes that may carry an order edge (an anchor of the tests
/// is not a pure node, docs/design/effects/edges.cn.md 4.6), committed as part of the program.
struct OrderChain {
    gc_node_ref_t a = kInvalidNodeRef;
    gc_node_ref_t b = kInvalidNodeRef;
    gc_node_ref_t c = kInvalidNodeRef;
};

/// Writes the chain into the root graph and commits it. Returns the committed program, or null
/// when the program has no root.
const Program *writeOrderChain(
    const camel::core::context::context_ptr_t &context, const Program *program, GraphId &rootId,
    OrderChain &chain) {
    if (program == nullptr || program->root() == nullptr) {
        return nullptr;
    }
    rootId = program->root()->id();
    RuntimeGraphDraftSession session(context, program);
    auto &draft = session.edit(program->root()).edit();
    chain.a           = anchorNode(draft);
    chain.b           = anchorNode(draft);
    chain.c           = anchorNode(draft);
    draft.appendOrderDependency(chain.b, chain.a);
    draft.appendOrderDependency(chain.c, chain.b);
    return session.commit();
}

/// Prints the order edges of a committed graph as `pred>user`, in the order the graph holds them.
void printOrderEdges(const Program *program, GraphId rootId, std::ostream &os) {
    const GCGraph *root = program->graph(rootId);
    os << "ORDER edges=" << (root != nullptr ? root->orderEdges().size() : 0);
    if (root != nullptr) {
        for (const GCGraphOrderEdge &edge : root->orderEdges()) {
            os << ' ' << edge.pred << '>' << edge.user;
        }
    }
    os << "\n";
}

} // namespace

// The orderings that pass through a removed node survive it: A -> B -> C becomes A -> C.
const camel::runtime::Program *
EdgesOrderSpliceTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    GraphId rootId;
    OrderChain chain;
    const Program *withChain = writeOrderChain(context_, program, rootId, chain);
    if (withChain == nullptr) {
        return program;
    }
    printOrderEdges(withChain, rootId, os);
    // The same call a dead-code pass makes to drop a statement the graph no longer needs.
    RuntimeGraphDraftSession session(context_, withChain);
    session.edit(withChain->graph(rootId)).edit().eraseNode(chain.b);
    const Program *committed = nullptr;
    try {
        committed = session.commit();
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return withChain;
    }
    os << "EDGES accepted\n";
    printOrderEdges(committed, rootId, os);
    return committed;
}

// A replacement takes the replaced node's order edges, both directions: A -> B -> C becomes
// A -> D -> C.
const camel::runtime::Program *
EdgesOrderReplaceTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    GraphId rootId;
    OrderChain chain;
    const Program *withChain = writeOrderChain(context_, program, rootId, chain);
    if (withChain == nullptr) {
        return program;
    }
    printOrderEdges(withChain, rootId, os);
    RuntimeGraphDraftSession session(context_, withChain);
    auto &draft = session.edit(withChain->graph(rootId)).edit();
    const gc_node_ref_t replacement = anchorNode(draft);
    draft.takeStatementPlace(chain.b, replacement);
    draft.eraseNode(chain.b);
    const Program *committed = nullptr;
    try {
        committed = session.commit();
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return withChain;
    }
    os << "EDGES accepted replacer=" << replacement << "\n";
    printOrderEdges(committed, rootId, os);
    return committed;
}

const camel::runtime::Program *
CommitMergeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    // A graph of the program the sessions reach, other than the root: the first session writes
    // the root, the second another graph, so the two edits do not meet.
    GCGraph *other = nullptr;
    {
        RuntimeGraphDraftSession probe(context_, program);
        for (GCGraph *graph : probe.collectReachableRuntimeGraphs()) {
            if (graph != program->root()) {
                other = graph;
                break;
            }
        }
    }
    if (other == nullptr) {
        os << "MERGE skip=1\n";
        return program;
    }
    const GraphId rootId      = program->root()->id();
    const GCGraph *rootBefore = program->graph(rootId);
    const GCGraph *otherBefore = program->graph(other->id());
    RuntimeGraphDraftSession first(context_, program);
    RuntimeGraphDraftSession second(context_, program);
    blankNode(first.edit(program->root()).edit());
    blankNode(second.edit(other).edit());
    const Program *one = first.commit();
    os << "MERGE stale=" << (context_->currentRuntimeProgram() == one ? 1 : 0) << "\n";
    // The second session started from the program the first replaced; its graph (and nothing it
    // wrote) changed in the program of the context, so the commit merges instead of forking.
    const Program *two = second.commit();
    const bool installed = context_->currentRuntimeProgram() == two;
    // The root the first commit made has one more node than the program it started from; the
    // merge keeps that version (or a version of the same content) and adds the second's graph.
    const GCGraph *mergedRoot  = two->graph(rootId);
    const GCGraph *mergedOther = two->graph(other->id());
    const bool keptFirst       = mergedRoot != nullptr && one->graph(rootId) != nullptr &&
                           mergedRoot->nodeCount() == one->graph(rootId)->nodeCount() &&
                           mergedRoot->nodeCount() > rootBefore->nodeCount();
    const bool keptSecond = mergedOther != nullptr && mergedOther != otherBefore;
    os << "MERGE installed=" << (installed ? 1 : 0) << " first_kept=" << (keptFirst ? 1 : 0)
       << " second_kept=" << (keptSecond ? 1 : 0)
       << " root_edited=" << (mergedRoot != rootBefore ? 1 : 0) << "\n";
    return two;
}

const camel::runtime::Program *
CommitConflictTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    GCGraph *other = nullptr;
    for (GCGraph *graph : program->graphs()) {
        if (graph != program->root()) {
            other = graph;
            break;
        }
    }
    RuntimeGraphDraftSession first(context_, program);
    RuntimeGraphDraftSession second(context_, program);
    RuntimeGraphDraftSession third(context_, program);
    blankNode(first.edit(program->root()).edit());
    blankNode(second.edit(program->root()).edit());
    if (other != nullptr) {
        third.edit(other).edit();
    }
    os << "CONFLICTS before=" << second.conflicts().size() << "\n";
    const Program *next = first.commit();
    os << "CONFLICTS same_graph=" << second.conflicts().size()
       << " other_graph=" << (other != nullptr ? third.conflicts().size() : 0) << "\n";
    try {
        second.commitChecked();
        os << "CHECKED accepted\n";
    } catch (const GraphCommitConflict &conflict) {
        os << "CHECKED refused: " << conflict.graphs().size() << " graph(s)\n";
    }
    return next;
}

const camel::runtime::Program *
EdgesSyncForkTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(program->root()).edit();
    const auto markStatement = [&](gc_node_ref_t node) {
        draft.setNodeRuntimeFlags(node, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    };
    // One statement two statements follow: what the splice leaves behind when it cannot move a
    // statement to the end of the chain growing from it (docs/design/effects/edges.cn.md 5.3).
    const gc_node_ref_t first  = anchorNode(draft);
    const gc_node_ref_t second = anchorNode(draft);
    const gc_node_ref_t third  = anchorNode(draft);
    markStatement(first);
    markStatement(second);
    markStatement(third);
    const gc_node_ref_t afterFirst[1] = {first};
    draft.setSyncInputs(second, afterFirst);
    draft.setSyncInputs(third, afterFirst);
    os << "SYNCFORK fork=" << first << ">" << second << ", " << first << ">" << third << "\n";
    try {
        const Program *next = session.commit();
        os << "EDGES accepted\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

const camel::runtime::Program *
EdgesSyncForkGateTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(program->root()).edit();
    const auto markStatement = [&](gc_node_ref_t node) {
        draft.setNodeRuntimeFlags(node, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    };
    // 5.3's other shape: the one chain end hands its value on, and a GATE does that for each block
    // that ends on it -- so two GATEs may follow one statement. It is an end of the chain, not a
    // continuation of it, and the seal check accepts it.
    const gc_node_ref_t tail = anchorNode(draft);
    markStatement(tail);
    for (float written : {2.0f, 3.0f}) {
        const gc_node_ref_t value = draft.materializeStaticValue(
            camel::core::rtdata::toSlot(written), camel::core::type::Type::Float32());
        const gc_node_ref_t gate = draft.addGateNode(camel::core::type::Type::Float32());
        draft.setNormInputs(gate, std::span<const gc_node_ref_t>(&value, 1));
        markStatement(gate);
        const gc_node_ref_t afterTail[1] = {tail};
        draft.setSyncInputs(gate, afterTail);
    }
    os << "SYNCFORKGATE tail=" << tail << "\n";
    try {
        const Program *next = session.commit();
        os << "EDGES accepted\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

const camel::runtime::Program *
EdgesSyncGateTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    const GraphId rootId = program->root()->id();
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(program->root()).edit();
    // The chain's tail: a statement a `sync` block wrote. A pass stands in for the builder here, so
    // it records the same fact the builder records (kGCNodeFlagSyncStatement): the seal check reads
    // it back, and a sync edge may only join statements of the block.
    const gc_node_ref_t tail = anchorNode(draft);
    draft.setNodeRuntimeFlags(tail, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    // The value the GATE lets out, and the GATE itself. The GATE is the block's value, which the
    // same flag covers: the chain's tail reaches the value through the GATE (5.3).
    const gc_node_ref_t value = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(2.0f),
        camel::core::type::Type::Float32());
    const gc_node_ref_t gate = draft.addGateNode(camel::core::type::Type::Float32());
    draft.setNormInputs(gate, std::span<const gc_node_ref_t>(&value, 1));
    draft.setNodeRuntimeFlags(gate, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    // The wait is the chain's tail, written as a sync edge (docs/design/effects/edges.cn.md 5.3: a
    // GATE has one data port and one sync predecessor, and no control input at all).
    const bool wrote = draft.setSyncPredecessor(gate, tail);
    os << "SYNCGATE wrote=" << (wrote ? 1 : 0) << " ctrl=" << draft.ctrlInputsOf(gate).size()
       << "\n";
    if (!wrote) {
        return program;
    }
    try {
        const Program *next = session.commit();
        const GCGraph *root = next->graph(rootId);
        os << "EDGES accepted\n";
        // What the graph the commit made holds, counted over every node: the node refs a draft
        // hands out are the graph's own only until it is encoded.
        size_t syncEdges   = 0;
        size_t gates       = 0;
        size_t gatesWaited = 0;
        size_t gatesCtrl   = 0;
        for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
            const auto preds = root->syncPredsOf(it.ref());
            syncEdges += preds.size();
            if (root->node(it.ref())->kind != GCNodeKind::Gate) {
                continue;
            }
            ++gates;
            gatesWaited += preds.empty() ? 0 : 1;
            gatesCtrl += root->ctrlInputsOf(it.ref()).empty() ? 0 : 1;
        }
        os << "SYNCGATE frozen_sync_edges=" << syncEdges << " gates=" << gates
           << " gates_waiting_on_the_chain=" << gatesWaited << " gates_with_ctrl=" << gatesCtrl
           << "\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}
