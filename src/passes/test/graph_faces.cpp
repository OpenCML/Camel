// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading of the facts the read face answers beside a node's record.

 * The encoder compares a draft's reading with the encoding's on every graph of every program
 * (`firstDifference`), and a comparison prints nothing when it holds. This pass asks the same two
 * faces for the same facts and prints both numbers, so a change in either answer shows up as a
 * changed line instead of as silence:

 *   [GF] regions D/F exit D/F slots D/F node N D/F
 *        the graph's own facts: how many regions it has, which node it answers, the size of its
 *        static area, and the region one node is in.
 *   [NF] dead <id past the end> draft=null frozen=null
 *        an id that names no node answers nothing on both faces.
 *   [NF] erased <id> draft=null frozen=record
 *        the one place the two faces do not agree: a draft keeps a place for an erased node so
 *        that the ids of the nodes it still has do not move, while an encoding is dropped of dead
 *        places (runtime/draft.h). Reading it needs the erasure to happen after the frozen side
 *        has answered, which is why the two sides are read in that order below.
 *   [BF] brch N arms D/F
 *        a branch node's arm count, read from a payload on both faces. Printed when the graph has
 *        a branch: a source-level `if` is folded away before an encoding unless both arms do
 *        something, and the cases whose graph keeps arms are the ones built on `arm_tail_gate`.

 * The frozen side is read first and its answers are kept as plain numbers: a draft decoded from a
 * graph borrows the graph's storage, so reading the frozen graph while the draft is open reads
 * what is being written.
 */

#include "camel/passes/test/graph_faces.h"

#include "camel/compile/gir/graph_read.h"
#include "camel/runtime/draft_session.h"

using camel::compile::gir::DraftGraphRead;
using camel::compile::gir::FrozenGraphRead;

namespace {

/// What a face answers about one graph, as plain numbers, so that the two readings can be taken
/// one after the other instead of at the same time.
struct Reading {
    uint32_t regions                     = 0;
    camel::runtime::gc_node_ref_t exitRef = camel::runtime::kInvalidNodeRef;
    size_t slots                         = 0;
    uint32_t nodeRegion                  = 0;
    camel::runtime::gc_cnt_t arms        = 0;
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

    // --- the frozen side, before anything is decoded ---------------------------------------
    gc_node_ref_t anyNode   = kInvalidNodeRef;
    gc_node_ref_t brchRef   = kInvalidNodeRef;
    gc_node_ref_t victimRef = kInvalidNodeRef;
    Reading frozen;
    {
        const FrozenGraphRead face(*target);
        for (gc_node_ref_t ref = 0; ref < target->nodeCount(); ++ref) {
            if (!face.alive(ref)) {
                continue;
            }
            if (anyNode == kInvalidNodeRef) {
                anyNode = ref;
            }
            if (face.kindOf(ref) == GCNodeKind::Brch) {
                brchRef = ref;
            }
            // The node erased later: the last one that is neither a port nor the graph's answer,
            // so that no list the rest of the pipeline reads keeps pointing at it.
            if (face.kindOf(ref) != GCNodeKind::Port && ref != face.exitRefOf()) {
                victimRef = ref;
            }
        }
        if (anyNode == kInvalidNodeRef) {
            return program;
        }
        frozen.regions    = face.regionCountOf();
        frozen.exitRef    = face.exitRefOf();
        frozen.slots      = face.staticSlotsOf().size();
        frozen.nodeRegion = face.regionOfNode(anyNode);
        frozen.arms       = face.armCountOf(brchRef);
    }

    // --- "names no node": both sides, frozen first ------------------------------------------
    const gc_node_ref_t past = static_cast<gc_node_ref_t>(target->nodeCount() + 5);
    bool deadFrozen          = true;
    bool erasedFrozen        = true;
    {
        const FrozenGraphRead face(*target);
        deadFrozen   = face.node(past) == nullptr;
        erasedFrozen = victimRef == kInvalidNodeRef || face.node(victimRef) == nullptr;
    }

    // --- the draft side, after the frozen side has answered --------------------------------
    Reading draft;
    bool deadDraft   = true;
    bool erasedDraft = true;
    {
        RuntimeGraphDraftSession reading(context_, program);
        GraphTransaction &txn = reading.edit(target);
        auto &editable        = txn.edit();
        const DraftGraphRead face(txn);
        draft.regions    = face.regionCountOf();
        draft.exitRef    = face.exitRefOf();
        draft.slots      = face.staticSlotsOf().size();
        draft.nodeRegion = face.regionOfNode(anyNode);
        draft.arms       = face.armCountOf(brchRef);
        deadDraft        = face.node(past) == nullptr;
        if (victimRef != kInvalidNodeRef) {
            editable.eraseNode(victimRef);
            erasedDraft = face.node(victimRef) == nullptr;
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
    if (brchRef != kInvalidNodeRef) {
        os << "[BF] brch " << brchRef << " arms " << draft.arms << "/" << frozen.arms << "\n";
    }
    os << "[GF] regions " << draft.regions << "/" << frozen.regions << " exit " << draft.exitRef
       << "/" << frozen.exitRef << " slots " << draft.slots << "/" << frozen.slots << " node "
       << anyNode << " " << draft.nodeRegion << "/" << frozen.nodeRegion << "\n";
    // The four lines below say the same things the lines above say with one graph's node numbers
    // in place of the relations those numbers stand for: `dead 16` reads like "node 16 is dead"
    // and means "the graph has 11 nodes", and `erased 10`/`brch 6`/`exit 5` are that graph's
    // indices. A plan that pins them pins one graph; a plan that reads these pins the shape. Both
    // are printed by the same run, so the two writings can be read against each other.
    os << "[GF] nodes " << target->nodeCount() << "\n";
    size_t brchCount = 0;
    gc_node_ref_t lastFree = kInvalidNodeRef;
    for (gc_node_ref_t ref = 0; ref < target->nodeCount(); ++ref) {
        const FrozenGraphRead face(*target);
        if (!face.alive(ref)) {
            continue;
        }
        if (face.kindOf(ref) == GCNodeKind::Brch) {
            ++brchCount;
        }
        if (face.kindOf(ref) != GCNodeKind::Port && ref != face.exitRefOf()) {
            lastFree = ref;
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
       << " frozen-record=" << ((!erasedFrozen && victimRef != kInvalidNodeRef) ? 1 : 0) << "\n";
    os << "[BF] brch at=" << brchRef << " only=" << (brchCount == 1 ? 1 : 0)
       << " arms-same=" << (draft.arms == frozen.arms ? 1 : 0) << " arms=" << draft.arms << "\n";
    os << "[GF] exit at=" << draft.exitRef << " same=" << (draft.exitRef == frozen.exitRef ? 1 : 0)
       << " answer=" << (draft.exitRef == frozen.exitRef && brchRef != kInvalidNodeRef ? 1 : 0)
       << "\n";
    return program;
}