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
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/cycle.h"
#include "camel/runtime/exec_plan.h"
#include "runtime/graph_build.h"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <span>
#include <vector>

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
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    switch (edit) {
    case Edit::Cycle: {
        const gc_node_ref_t a = blankNode(transaction);
        const gc_node_ref_t b = blankNode(transaction);
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
        blankNode(*first);
        auto *afterEdit = &session.edit(program->root());
        if (first == second && first == afterEdit) {
            os << "EDGES transaction stable\n";
        } else {
            os << "EDGES transaction moved: " << first << " " << second << " " << afterEdit << "\n";
        }
        blankNode(*first);
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
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const gc_node_ref_t first  = anchorNode(transaction);
    const gc_node_ref_t second = anchorNode(transaction);
    const gc_node_ref_t third  = anchorNode(transaction);
    os << "ORDER wrote=" << (draft.appendOrderDependency(second, first) ? 1 : 0)
       << " twice=" << (draft.appendOrderDependency(second, first) ? 1 : 0) << "\n";
    os << "ORDER preds=" << draft.orderInputsOf(second).size()
       << " users=" << draft.orderUsersOf(first).size() << "\n";
    // Two predecessors of one user, appended in the order that is not their numbering order: the
    // frozen graph writes one node's side edges by the predecessor's number, so what a dump of it
    // prints does not depend on the order a rewrite collected them in.
    os << "ORDER wrote_reversed=" << (draft.appendOrderDependency(third, second) ? 1 : 0)
       << (draft.appendOrderDependency(third, first) ? 1 : 0) << "\n";
    try {
        const Program *next = session.commit();
        const GCGraph *root = next->graph(rootId);
        os << "EDGES accepted\n";
        // The graph holds the edge in its own array, and the per-node reader (the one every
        // scheduler and pass uses) agrees with that array node for node.
        size_t seen = 0;
        size_t ordered = 0;
        bool sorted    = true;
        for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
            const auto preds = root->orderPredsOf(it.ref());
            seen += preds.size();
            ordered += preds.empty() ? 0 : 1;
            for (size_t i = 1; i < preds.size(); ++i) {
                if (preds[i - 1].pred > preds[i].pred) {
                    sorted = false;
                }
            }
        }
        os << "ORDER frozen_agrees=" << (seen == root->orderEdges().size() ? 1 : 0)
           << " some_node_ordered=" << (ordered != 0 ? 1 : 0)
           << " frozen_sorted=" << (sorted ? 1 : 0) << "\n";
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
    // A negative case must be built in the layer that owns the invariant. The seal check owns "no
    // order edge on a pure node" (docs/design/effects/edges.cn.md 4.6) and it reads a finished
    // graph; the draft only writes the edges a rewrite asks for. Since the encoder drops an order
    // edge whose user takes no order (planDraftPayload), a bad graph built through a draft is never
    // written at all: the case would print "wrote=1" and then watch the commit accept a graph
    // without the edge. So the case builds the bad graph *here*, on the frozen records, and hands it
    // to the seal check. The draft's half is asserted to be "no longer produced" instead.
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const gc_node_ref_t anchor = anchorNode(transaction);
    // A constant is a pure node: nothing about it can be ordered.
    const gc_node_ref_t constant = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(1.0f), camel::core::type::Type::Float32());
    os << "ORDER wrote=" << (draft.appendOrderDependency(constant, anchor) ? 1 : 0) << "\n";
    // The encoder's own reading: the edge was asked for and the plan drops it, because the node it
    // would order takes no order -- so the graph the draft hands the commit does not have it.
    const GraphId rootId = program->root()->id();
    const Program *next  = session.commit();
    const GCGraph *root  = next != nullptr ? next->graph(rootId) : nullptr;
    os << "ORDER frozen_edges=" << (root != nullptr ? root->orderEdges().size() : 0) << "\n";

    // The bad graph itself: two pure nodes and an order edge on the user of them. It is written
    // straight into the frozen form the seal check reads, so nothing in the builder can drop it.
    GCGraphPayloadShape shape;
    shape.nodeCount       = 2;
    shape.orderEdgeCount  = 1;
    std::vector<GraphId> noGraphs;
    const auto sameType = camel::core::type::Type::Float32()->typeId();
    GCGraph *bad        = GCGraphBuildAccess::create(
        context_->graphIds().allocate(),
        GCGraphDerivation{},
        createGraphDebugRecord("test$order_pure", "test$order_pure"),
        camel::core::type::FunctionType::create(),
        std::nullopt,
        nullptr,
        nullptr,
        kInvalidGraphId,
        noGraphs,
        noGraphs,
        noGraphs,
        noGraphs,
        shape,
        [&](GCGraphPayloadArena &arena) {
            for (gc_node_ref_t ref = 0; ref < 2; ++ref) {
                GCNode node;
                node.kind   = GCNodeKind::Data;
                node.typeId = sameType;
                arena.nodes[ref] = node;
            }
            arena.nodes[2] = GCNode{};
            arena.orderEdges[0] = GCGraphOrderEdge{.user = 0, .pred = 1};
            arena.entryNode  = 0;
            arena.exitNode   = 1;
            arena.outputNode = 0;
        },
        std::span<const slot_t>{});
    try {
        validateRuntimeGraphEdges(*program, bad);
        os << "EDGES accepted\n";
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
    }
    return program;
}

const camel::runtime::Program *
EdgesOrderScheduleTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    const GraphId rootId = program->root()->id();
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const gc_node_ref_t exit = draft.exitNode();
    if (exit == kInvalidNodeRef) {
        os << "ORDER no_exit=1\n";
        return program;
    }
    // Three anchors: two the exit waits for, so both run and neither reaches the other, and one
    // nothing waits for, so it does not run at all.
    const gc_node_ref_t first  = anchorNode(transaction);
    const gc_node_ref_t second = anchorNode(transaction);
    const gc_node_ref_t never  = anchorNode(transaction);
    const gc_node_ref_t late   = anchorNode(transaction);
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
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    chain.a           = anchorNode(transaction);
    chain.b           = anchorNode(transaction);
    chain.c           = anchorNode(transaction);
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
    auto &transaction = session.edit(withChain->graph(rootId));
    auto &draft       = transaction.edit();
    const gc_node_ref_t replacement = anchorNode(transaction);
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
    blankNode(first.edit(program->root()));
    blankNode(second.edit(other));
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
    blankNode(first.edit(program->root()));
    blankNode(second.edit(program->root()));
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
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const auto markStatement = [&](gc_node_ref_t node) {
        draft.setNodeRuntimeFlags(node, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    };
    // One statement two statements follow: what the splice leaves behind when it cannot move a
    // statement to the end of the chain growing from it (docs/design/effects/edges.cn.md 5.3).
    const gc_node_ref_t first  = anchorNode(transaction);
    const gc_node_ref_t second = anchorNode(transaction);
    const gc_node_ref_t third  = anchorNode(transaction);
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
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const auto markStatement = [&](gc_node_ref_t node) {
        draft.setNodeRuntimeFlags(node, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    };
    // 5.3's other shape: the one chain end hands its value on, and a GATE does that for each block
    // that ends on it -- so two GATEs may follow one statement. It is an end of the chain, not a
    // continuation of it, and the seal check accepts it.
    const gc_node_ref_t tail = anchorNode(transaction);
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
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    // The chain's tail: a statement a `sync` block wrote. A pass stands in for the builder here, so
    // it records the same fact the builder records (kGCNodeFlagSyncStatement): the seal check reads
    // it back, and a sync edge may only join statements of the block.
    const gc_node_ref_t tail = anchorNode(transaction);
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

// The shared judgement, on the side that has no back end to protect it: two statements that each
// follow the other is a cycle on sync edges (docs/design/effects/edges.cn.md section 1), and
// camel/runtime/cycle.h walks sync with the other four kinds, so both the draft's check and the
// freeze's walk must name it. The pass prints what the draft's side reports -- found, and whether
// the chain it returns holds both nodes -- and then what the commit does with the same graph.
const camel::runtime::Program *
EdgesSyncCycleTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const auto markStatement = [&](gc_node_ref_t node) {
        draft.setNodeRuntimeFlags(node, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    };
    const gc_node_ref_t a = anchorNode(transaction);
    const gc_node_ref_t b = anchorNode(transaction);
    markStatement(a);
    markStatement(b);
    const gc_node_ref_t afterA[1] = {a};
    const gc_node_ref_t afterB[1] = {b};
    draft.setSyncInputs(a, afterB);
    draft.setSyncInputs(b, afterA);
    DraftCycleEdges edges(draft);
    std::vector<gc_node_ref_t> path;
    const gc_node_ref_t closes = findInputCycle(edges, &path);
    bool hasA                  = false;
    bool hasB                  = false;
    for (gc_node_ref_t node : path) {
        hasA = hasA || node == a;
        hasB = hasB || node == b;
    }
    os << "SYNCCYCLE a=" << a << " b=" << b << " found="
       << (closes == kInvalidNodeRef ? 0 : 1) << " on_cycle_a=" << (hasA ? 1 : 0)
       << " on_cycle_b=" << (hasB ? 1 : 0) << "\n";
    try {
        const Program *next = session.commit();
        os << "EDGES accepted\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

// One node read in every kind there is, and the two readings of its users. The draft keeps one
// list per edge kind as the pass writes each edge; the graph the commit makes keeps one table
// built from the input runs, with the sync users beside them (a sync edge is not a port). A pass
// asking what reads or waits on a node (camel/runtime/effect_liveness.h) must get the same answer
// from either side -- the graph had no accessor for four of those lists at all before this case
// existed -- so the sets are compared node for node, in the runtime refs the commit gives the
// draft's nodes (a draft's ids stop meaning anything once it is encoded).
const camel::runtime::Program *
EdgeUsersTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    const GraphId rootId = program->root()->id();
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(program->root());
    auto &draft       = transaction.edit();
    const auto markStatement = [&](gc_node_ref_t node) {
        draft.setNodeRuntimeFlags(node, static_cast<uint8_t>(kGCNodeFlagSyncStatement));
    };
    // The one node all four kinds reach: a value port (twice, so the four counts are not all
    // equal), a context port, a control input, and a place on a `sync` block's chain.
    const gc_node_ref_t source = anchorNode(transaction);
    markStatement(source);
    for (size_t i = 0; i < 2; ++i) {
        const gc_node_ref_t reader = draft.addCopyNode(camel::core::type::Type::Float32());
        draft.setNormInputs(reader, std::span<const gc_node_ref_t>(&source, 1));
    }
    // A GATE reads a value of its own and a context port of `source`; its wait is a control input,
    // the kind the seal accepts on a gate.
    const gc_node_ref_t wait = anchorNode(transaction);
    const gc_node_ref_t constant = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(4.0f), camel::core::type::Type::Float32());
    const gc_node_ref_t contextual = draft.addGateNode(camel::core::type::Type::Float32());
    draft.setNormInputs(contextual, std::span<const gc_node_ref_t>(&constant, 1));
    draft.setCtrlInputs(contextual, std::span<const gc_node_ref_t>(&wait, 1));
    draft.setWithInputs(contextual, std::span<const gc_node_ref_t>(&source, 1));
    const gc_node_ref_t controller = anchorNode(transaction);
    draft.setCtrlInputs(controller, std::span<const gc_node_ref_t>(&source, 1));
    const gc_node_ref_t follower = anchorNode(transaction);
    markStatement(follower);
    if (!draft.setSyncPredecessor(follower, source)) {
        os << "EDGE_USERS wrote=0\n";
        return program;
    }
    // The runtime ref of every live node of the draft, read before the commit, which encodes the
    // drafts as they are now.
    const std::vector<gc_node_ref_t> committed = session.committedNodeRefs(program->root());
    const auto runtimeRefOf = [&](gc_node_ref_t id) {
        return id < committed.size() ? committed[id] : kInvalidNodeRef;
    };
    const auto ordered = [](std::vector<gc_node_ref_t> refs) {
        std::sort(refs.begin(), refs.end());
        refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
        return refs;
    };
    const auto draftReading = [&](std::span<const gc_node_ref_t> users) {
        std::vector<gc_node_ref_t> refs;
        for (gc_node_ref_t user : users) {
            refs.push_back(runtimeRefOf(user));
        }
        return ordered(std::move(refs));
    };
    std::vector<std::array<std::vector<gc_node_ref_t>, 4>> draftUsers(committed.size());
    for (gc_node_ref_t id = 0; id < committed.size(); ++id) {
        if (committed[id] == kInvalidNodeRef) {
            continue;
        }
        std::array<std::vector<gc_node_ref_t>, 4> &sets = draftUsers[id];
        sets[0] = draftReading(draft.normUsersOf(id));
        sets[1] = draftReading(draft.withUsersOf(id));
        sets[2] = draftReading(draft.ctrlUsersOf(id));
        sets[3] = draftReading(draft.syncUsersOf(id));
    }
    const Program *next = nullptr;
    try {
        next = session.commit();
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
    os << "EDGES accepted\n";
    const GCGraph *root     = next->graph(rootId);
    size_t disagreeingNodes = 0;
    for (gc_node_ref_t id = 0; id < committed.size(); ++id) {
        const gc_node_ref_t ref = committed[id];
        if (ref == kInvalidNodeRef) {
            continue;
        }
        const std::array<std::span<const gc_node_ref_t>, 4> graphReading{
            root->normUsersOf(ref),
            root->withUsersOf(ref),
            root->ctrlUsersOf(ref),
            root->syncUsersOf(ref)};
        for (size_t kind = 0; kind < graphReading.size(); ++kind) {
            const std::vector<gc_node_ref_t> fromGraph(
                graphReading[kind].begin(), graphReading[kind].end());
            if (ordered(fromGraph) != draftUsers[id][kind]) {
                ++disagreeingNodes;
                break;
            }
        }
    }
    const gc_node_ref_t sourceRef = runtimeRefOf(source);
    os << "EDGE_USERS norm=" << root->normUsersOf(sourceRef).size()
       << " with=" << root->withUsersOf(sourceRef).size()
       << " ctrl=" << root->ctrlUsersOf(sourceRef).size()
       << " sync=" << root->syncUsersOf(sourceRef).size() << " nodes=" << committed.size()
       << " disagree=" << disagreeingNodes << "\n";
    return next;
}
