// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of the facts the view answers about a graph, and of the check that the two
 * readings of one graph agree.
 *
 * The view (`kernel/graph/graph_view.h`) is read through two adapters: one over a frozen graph and
 * one over the draft it is decoded to. The encoder asks the same check of every graph of every
 * program (`camel::graph::firstDifference`), and a comparison that holds prints nothing; this pass
 * asks the two adapters for the same facts and prints both numbers, so a change in either answer
 * shows up as a changed line instead of as silence:

 *   [GF] regions D/F exit D/F slots D/F node N D/F
 *        the graph's own facts: how many regions it has, which node it answers, the size of its
 *        static area, and the region one node is in. The static area is the one field of the two
 *        readings the view does not answer on purpose (the interface review took it off the face:
 *        which slot a value sits in is the execution plan's), so it is read from the two
 *        representations here -- the pass is comparing the representations, not asking a reader's
 *        question.
 *   [NF] dead <id past the end> draft=null frozen=null
 *        an id that names no node answers nothing on both sides.
 *   [NF] erased <id> draft=null frozen=record
 *        the one place the two readings do not agree: a draft keeps a place for an erased node so
 *        that the ids of the nodes it still has do not move, while an encoding is dropped of dead
 *        places (runtime/draft.h). Reading it needs the erasure to happen after the frozen side
 *        has answered, which is why the two sides are read in that order below.
 *   [BF] brch N arms D/F
 *        a branch node's arms, read as the regions it owns: the view has no arm count, because a
 *        count is the size of that list. Printed when the graph has a branch: a source-level `if`
 *        is folded away before an encoding unless both arms do something, and the cases whose graph
 *        keeps arms are the ones built on `arm_tail_gate`.
 *   [VF] agree=1
 *        the view's consistency check (`camel::graph::firstDifference`) over the two adapters: the
 *        same graph read twice has to be the same graph. It is the same check the encoder asks of
 *        every graph it writes, printed here so that a change in either adapter is a changed line
 *        in the fast layer, and the difference itself is printed when there is one.

 * The frozen side is read first and its answers are kept as plain numbers: a draft decoded from a
 * graph borrows the graph's storage, so reading the frozen graph while the draft is open reads
 * what is being written.
 */

#include "camel/passes/test/graph_faces.h"

#include "camel/execute/pass/registration.h"
#include "camel/runtime/draft_session.h"
#include "graph/callee_graph_of.h"
#include "graph/consistency.h"
#include "graph/draft_view.h"
#include "graph/gc_graph_view.h"
#include "graph/graph_view.h"

#include <string>

using camel::graph::DraftGraphAdapter;
using camel::graph::FrozenGraphAdapter;
using camel::graph::GraphView;
using camel::graph::NodeKind;
using camel::graph::NodeRef;

namespace {

/// What a reading answers about one graph, as plain numbers, so that the two readings can be taken
/// one after the other instead of at the same time.
struct Reading {
    uint32_t regions  = 0;
    NodeRef exitRef   = camel::graph::kNoNode;
    size_t slots      = 0;
    uint32_t nodeRegion = camel::graph::kNoRegion;
    size_t arms       = 0;
};

} // namespace

const camel::runtime::Program *
GraphFacesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr) {
        return program;
    }
    // The largest graph, so that there is a region beyond the function's own to ask about.
    GCGraph *target = nullptr;
    for (GCGraph *graph : program->graphs()) {
        if (target == nullptr || graph->regionCount() > target->regionCount() ||
            (graph->regionCount() == target->regionCount() &&
             graph->nodeCount() > target->nodeCount())) {
            target = graph;
        }
    }
    if (target == nullptr || target->nodeCount() < 4) {
        return program;
    }
    // A callee is named by value, and the program is the table that holds those graphs
    // (graph/callee_graph_of.h): the same resolver the encoder gives its check.
    const camel::graph::CalleeGraphOf calleeGraphOf =
        [program](GraphId id) -> const camel::graph::Graph * { return program->graph(id); };

    // --- the frozen side, before anything is decoded ---------------------------------------
    NodeRef anyNode   = camel::graph::kNoNode;
    NodeRef brchRef   = camel::graph::kNoNode;
    NodeRef victimRef = camel::graph::kNoNode;
    Reading frozen;
    {
        const FrozenGraphAdapter view(*target, calleeGraphOf);
        for (const NodeRef ref : view.nodes()) {
            if (anyNode == camel::graph::kNoNode) {
                anyNode = ref;
            }
            // A BRCH is the kind that owns arms: the JOIN a branch is closed by folds into the same
            // kind (the view has one kind for both), and it owns nothing.
            if (view.kindOf(ref) == NodeKind::Brch && !view.regionsOwnedBy(ref).empty()) {
                brchRef = ref;
            }
            // The node erased later: the last one that is neither a port nor the graph's answer,
            // so that no list the rest of the pipeline reads keeps pointing at it.
            if (view.kindOf(ref) != NodeKind::Port && ref != view.exit()) {
                victimRef = ref;
            }
        }
        if (anyNode == camel::graph::kNoNode) {
            return program;
        }
        frozen.regions    = view.regionCount();
        frozen.exitRef    = view.exit();
        // The static area is not a query of the view (the file comment): both readings keep the
        // same words here, so the count is the frozen graph's own.
        frozen.slots      = target->staticSlots().size();
        frozen.nodeRegion = view.regionOf(anyNode);
        frozen.arms       = brchRef == camel::graph::kNoNode
                                ? 0
                                : view.regionsOwnedBy(brchRef).size();
    }

    // --- "names no node": both sides, frozen first ------------------------------------------
    const NodeRef past = static_cast<NodeRef>(target->nodeCount() + 5);
    bool deadFrozen    = true;
    bool erasedFrozen  = true;
    {
        const FrozenGraphAdapter view(*target, calleeGraphOf);
        deadFrozen   = !view.contains(past);
        erasedFrozen = victimRef == camel::graph::kNoNode || !view.contains(victimRef);
    }

    // --- the view's consistency check, before the erasure -----------------------------------
    bool viewAgrees = true;
    std::string viewDifference;
    {
        RuntimeGraphDraftSession check(context_, program);
        GraphTransaction &txn = check.edit(target);
        const DraftGraphAdapter draftView(txn, calleeGraphOf);
        const FrozenGraphAdapter frozenView(*target, calleeGraphOf);
        viewDifference = camel::graph::firstDifference(draftView, frozenView);
        viewAgrees     = viewDifference.empty();
    }

    // --- the draft side, after the frozen side has answered --------------------------------
    Reading draft;
    bool deadDraft   = true;
    bool erasedDraft = true;
    {
        RuntimeGraphDraftSession reading(context_, program);
        GraphTransaction &txn = reading.edit(target);
        auto &editable        = txn.edit();
        const DraftGraphAdapter view(txn, calleeGraphOf);
        draft.regions    = view.regionCount();
        draft.exitRef    = view.exit();
        draft.slots      = txn.graph().staticSlots().size();
        draft.nodeRegion = view.regionOf(anyNode);
        draft.arms = brchRef == camel::graph::kNoNode ? 0 : view.regionsOwnedBy(brchRef).size();
        deadDraft  = !view.contains(past);
        if (victimRef != camel::graph::kNoNode) {
            editable.eraseNode(victimRef);
            erasedDraft = !view.contains(victimRef);
        }
        // The erasure is never committed: this pass reports and leaves the program it was given.
    }

    // The absolute readings this pass has always printed, kept beside the relations below so a run
    // prints both: a plan that pins these pins this graph, a plan that reads those pins the shape,
    // and one run shows the two agree wherever both speak.
    os << "[NF] dead " << past << " draft=" << (deadDraft ? "null" : "record")
       << " frozen=" << (deadFrozen ? "null" : "record") << "\n";
    os << "[NF] erased " << victimRef << " draft=" << (erasedDraft ? "null" : "record")
       << " frozen=" << (erasedFrozen ? "null" : "record") << "\n";
    if (brchRef != camel::graph::kNoNode) {
        os << "[BF] brch " << brchRef << " arms " << draft.arms << "/" << frozen.arms << "\n";
    }
    os << "[GF] regions " << draft.regions << "/" << frozen.regions << " exit " << draft.exitRef
       << "/" << frozen.exitRef << " slots " << draft.slots << "/" << frozen.slots << " node "
       << anyNode << " " << draft.nodeRegion << "/" << frozen.nodeRegion << "\n";
    // The lines below say the same things the lines above say with one graph's node numbers in
    // place of the relations those numbers stand for: `dead 16` reads like "node 16 is dead" and
    // means "the graph has 11 nodes", and `erased 10`/`brch 6`/`exit 5` are that graph's indices. A
    // plan that pins them pins one graph; a plan that reads these pins the shape. Both are printed
    // by the same run, so the two writings can be read against each other.
    os << "[GF] nodes " << target->nodeCount() << "\n";
    size_t brchCount       = 0;
    NodeRef lastFree       = camel::graph::kNoNode;
    {
        const FrozenGraphAdapter view(*target, calleeGraphOf);
        for (const NodeRef ref : view.nodes()) {
            if (view.kindOf(ref) == NodeKind::Brch && !view.regionsOwnedBy(ref).empty()) {
                ++brchCount;
            }
            if (view.kindOf(ref) != NodeKind::Port && ref != view.exit()) {
                lastFree = ref;
            }
        }
    }
    const auto beyondTheEnd = [&] {
        return past > target->nodeCount();
    };
    os << "[NF] dead past=" << past << " gap=" << (past - target->nodeCount())
       << " beyond=" << (beyondTheEnd() ? 1 : 0)
       << " both=" << ((deadDraft && deadFrozen) ? 1 : 0) << "\n";
    os << "[NF] erased at=" << victimRef << " last=" << (victimRef == lastFree ? 1 : 0)
       << " draft-null=" << (erasedDraft ? 1 : 0)
       << " frozen-record="
       << ((!erasedFrozen && victimRef != camel::graph::kNoNode) ? 1 : 0) << "\n";
    os << "[BF] brch at=" << brchRef << " only=" << (brchCount == 1 ? 1 : 0)
       << " arms-same=" << (draft.arms == frozen.arms ? 1 : 0) << " arms=" << draft.arms << "\n";
    os << "[GF] exit at=" << draft.exitRef << " same=" << (draft.exitRef == frozen.exitRef ? 1 : 0)
       << " answer=" << (draft.exitRef == frozen.exitRef && brchRef != camel::graph::kNoNode ? 1 : 0)
       << "\n";
    os << "[VF] agree=" << (viewAgrees ? 1 : 0)
       << (viewAgrees ? "" : (std::string(" ") + viewDifference)) << "\n";
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel graphFacesKernel(kGraphFacesTestPass, makePass<GraphFacesTestPass>());

} // namespace