// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * DraftGraphAdapter: `GraphView` over the graph a session is building or editing (`GraphDraft`,
 * camel/runtime/draft.h), reached through the `GraphTransaction` that owns it.
 *
 * The second of the two transitional adapters stage 2.1 asks for. Replaces
 * `camel::compile::gir::DraftGraphRead` (`compile/include/camel/compile/gir/graph_read.h`), which
 * is the same idea stopped at the facts the derivative lift asks, together with its `kept` id table
 * -- that table stays here because it is the only way a draft's ids and a graph's ids can be the
 * same answer, and this adapter keeps it for the same reason.
 *
 * Who migrates to it: the draft side of `firstDifference` and of the lift,
 * `lib/test/graph_faces.cpp`, then `LoopDraftView` and `decidingRegionOfDraft` -- the drafts of
 * each pair of predicates section 2 counts, which stop existing rather than move.
 *
 * It answers about a graph that is still being changed, so the one thing it must not do is hand
 * back a span into a buffer an edit can move. Where the frozen adapter answers a span into the
 * graph, this one answers a span into the node's own tail, which only an edit of *that node* moves.
 *
 * The mapping is the frozen adapter's table read off the draft's fields; the differences are these,
 * and they are the representation's, not the view's:
 *
 *   DraftNodeHeader::kind                    kindOf            (same four-kind folding)
 *   DraftNodeHeader::dataType                typeOf
 *   DraftNodeHeader::runtimeFlags            nothing: a macro is the record of the callee or of
 *                                            the operator (2.2), a constant is `constantOf`
 *   DraftNodeHeader::origin                  originOf
 *   DraftNodeHeader::region                  regionOf
 *   DraftNodeView::normInputs/withInputs     inputs(ref, Normal) / inputs(ref, Context)
 *   DraftNodeHeader::ctrlInputCount          nothing: a control input is not an edge, and what it
 *                                            said is the region the node is in
 *   normUsersOf / withUsersOf                users(ref, Normal) / users(ref, Context)
 *   (both input groups at once)              predecessors(ref, Data)
 *   DraftNodeView::syncInputs                predecessors(ref, Sync)
 *   DraftNodeView::orderInputs               predecessors(ref, Order)
 *   GraphDraft::region / nodeRegion          region / regionOf
 *   regionPort(i) + GCRegion.portBegin/Count regionParameters
 *   armRegionOf / loopRegionOf               regionsOwnedBy
 *   payloadGraphs (by index) + `kept`        calleeOf, as the graph that id names (by value)
 *   GCOperBody in the payload tail           operatorOf         (same refusal: see gc_graph_view.h)
 *   LOOP payload                             loopFormOf
 *   entryNode / exitNode                     exit
 *   closureNodes                             ports(Capture)
 *   closureType                              nothing: the captures' types are ports(Capture)
 *   staticSlots                              constantOf, per DATA node (read from staticSlots)
 *   DraftNodeHeader::dataIndex               nothing: which slot a value sits in is the execution
 *                                            plan's (section 13)
 *
 * `kept`, when it is given, is the same id table the encoder builds (`liveDraftNodes`): `kept[i]`
 * is the draft's node the runtime graph's node `i` is built from. A draft read beside the graph it
 * was encoded to must use it, because encoding drops the front end's placeholders and numbers what
 * is left from zero; a draft read on its own answers its own ids, which is what a lift reading a
 * draft it just built wants.
 */

#pragma once

#include "camel/runtime/graph_txn.h"
#include "graph/graph_view.h"

#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace camel::graph {

static_assert(
    std::is_same_v<NodeRef, camel::runtime::gc_node_ref_t>,
    "The view's NodeRef is the draft's gc_node_ref_t, which is the frozen graph's.");

class DraftGraphAdapter final : public GraphView {
  public:
    /// The face of a draft read on its own: its own ids, no table.
    explicit DraftGraphAdapter(const camel::runtime::GraphTransaction &transaction)
        : transaction_(transaction) {}

    /// The face of a draft read beside the graph it was encoded to, through the encoder's id table.
    DraftGraphAdapter(
        const camel::runtime::GraphTransaction &transaction, std::span<const NodeRef> kept)
        : transaction_(transaction), kept_(kept) {}

    camel::core::type::FunctionType *funcType() const override;
    std::vector<NodeRef> ports(PortGroup group) const override;
    NodeRef exit() const override;

    std::uint32_t nodeSlotCount() const override;
    bool contains(NodeRef ref) const override;
    NodeKind kindOf(NodeRef ref) const override;
    const camel::core::type::Type *typeOf(NodeRef ref) const override;
    std::optional<::slot_t> constantOf(NodeRef ref) const override;
    std::uint64_t originOf(NodeRef ref) const override;

    std::span<const NodeRef> inputs(NodeRef ref, PortGroup group) const override;
    std::span<const NodeRef> users(NodeRef ref, PortGroup group) const override;
    std::vector<NodeRef> predecessors(NodeRef ref, EdgeKind kind) const override;

    std::uint32_t regionCount() const override;
    RegionRecord region(RegionId id) const override;
    std::span<const NodeRef> regionParameters(RegionId id) const override;
    std::vector<NodeRef> regionNodes(RegionId id) const override;
    RegionId regionOf(NodeRef ref) const override;
    bool isAncestorOf(RegionId ancestor, RegionId region) const override;
    std::vector<RegionId> regionsOwnedBy(NodeRef ref) const override;

    CalleeRef calleeOf(NodeRef ref) const override;
    camel::core::OperId operatorOf(NodeRef ref) const override;
    LoopForm loopFormOf(NodeRef ref) const override;

  private:
    /// The draft's node the view's node `ref` is: `ref` itself when no table was given, else
    /// `kept_[ref]`, and `kNoNode` when the table says the runtime graph has no such node (a
    /// placeholder, or one outside the reachable set).
    NodeRef sourceOf(NodeRef ref) const;
    /// One of the draft's ids as the view's: `kept` reversed, or `kNoNode` when the node it names
    /// is not one the runtime graph has. Only meaningful with a table.
    NodeRef viewIdOf(NodeRef draftId) const;
    /// A list of the draft's ids translated the way `inputs` translates one.
    std::vector<NodeRef> viewIdsOf(std::span<const NodeRef> draftIds) const;

    static NodeKind viewKind(camel::runtime::GCNodeKind stored);
    static bool awaitsOperatorRecord(camel::runtime::GCNodeKind stored);

    const camel::runtime::GraphTransaction &transaction_;
    std::span<const NodeRef> kept_;
};

} // namespace camel::graph