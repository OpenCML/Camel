// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * FrozenGraphAdapter: `GraphView` over a frozen graph (`GCGraph`, camel/runtime/graph.h).
 *
 * The transitional adapter stage 2.1 asks for ("a frozen graph and a draft, one adapter each,
 * deleted again by stage 3").
 *
 * Who reads through it: the encoder's self-check (`compile/src/gir/graph_encode.cpp`,
 * `firstDifference`), `LoopView`, `ControlRegions`, `NodeRoleRegistry`, `lib/test/graph_faces.cpp`,
 * and the freeze side of every predicate the view is meant to answer once.
 *
 * It answers; it does not decide, and it stores nothing the graph does not. The four readings that
 * differ from the stored graph are the file comment's, and the mapping is this table -- a reader
 * that needs a stored field this table does not name is asking a question the view does not answer
 * and the model has to grow first:
 *
 *   stored                                   view
 *   GCNodeKind::Data                         NodeKind::Data
 *   GCNodeKind::Port                         NodeKind::Port
 *   GCNodeKind::Brch                         NodeKind::Brch      (its JOIN is not a node here)
 *   GCNodeKind::Loop                         NodeKind::Loop
 *   GCNodeKind::Func / Call / Gate           NodeKind::Func / Call / Gate
 *   GCNodeKind::Bind                         NodeKind::Bind      (reserved: nothing makes one)
 *   GCNodeKind::Oper                         NodeKind::Oper
 *   GCNodeKind::Cast / Copy / Fill / Accs    NodeKind::Oper      -- see the four kinds below
 *   GCNodeKind::Join / Dref / Dead           no node: `contains` is false, and the NodeRef is a gap
 *
 *   GCNode::kind                             kindOf
 *   GCNode::dataType                         typeOf
 *   GCNode::flags (macro, constant)          nothing: a macro is the record of the callee or of
 *                                            the operator (2.2), a constant is `constantOf`
 *   normInputsOf / withInputsOf              inputs(ref, Normal) / inputs(ref, Context)
 *   normUsersOf / withUsersOf                users(ref, Normal) / users(ref, Context)
 *   normInputsOf / withInputsOf / both       predecessors(ref, Data)
 *   syncPredsOf                              predecessors(ref, Sync)     (edge.pred)
 *   orderPredsOf                             predecessors(ref, Order)    (edge.pred)
 *   regionCount / region / nodeRegion        regionCount / region / regionOf
 *   regionPorts / (nodes of a region)        regionParameters / regionNodes
 *   GCRegion{kind,owner,index,parent,result} RegionRecord, copied
 *   loopKindOf, loopIterableFormOf,
 *   loopCollectionCountOf, loopBodyOf        loopFormOf, as one record
 *   calleeIdOf / GCFuncRef                   calleeOf, as the graph that id names (by value)
 *   GCOperBody.oper / GCOperBody.uri         operatorOf (the URI comes from the operator table)
 *   armCountOf / loopRegionOf                regionsOwnedBy (a count is that list's size)
 *   GCGraph::exitNodeRef                     exit
 *   normPorts / withPorts / closureNodes     ports(Normal) / ports(Context) / ports(Capture)
 *   closureType                              nothing: the captures' types are ports(Capture)
 *   staticSlots                              constantOf, per DATA node (read from staticSlots)
 *   GCNode::dataIndex                        nothing: which slot a value sits in is the execution
 *                                            plan's (section 13)
 *   nodeOrigin                               originOf
 *   nodePayloadBytes / nodeBodyAs            nothing: no payload bytes leave this face
 *
 * The four kinds and `operatorOf`: `kindOf` answers `NodeKind::Oper` for a CAST, a COPY, a FILL and
 * an ACCS (so the shape of the graph is already the target's), and `operatorOf` answers `kNoOper`
 * for them, because neither representation gives those four an operator record yet -- they keep
 * their own payload (a kind, a place, a template). That is what the old face said too (an empty URI
 * from `operUriOf`, which asked a stored OPER), so a reader that asks "which operator does this node
 * name" gets exactly the answer it got before, and a reader that asks "is this an operator" asks the
 * kind. Plan 2.3 declares the four as operators with records, and that is the change which fills
 * this gap -- in one place, here.
 *
 * `view()` on the graph (`camel/graph/graph.h`) hands out this adapter: a reader holding a
 * `const Graph *` -- the callee of a FUNC node, the anchor of a fact -- reads that graph again
 * through the view. The adapter answers every question but naming a callee, which needs the program
 * that holds the graphs (`CalleeGraphOf`); that one is refused rather than guessed, because a graph
 * does not know its program.
 */

#pragma once

#include "camel/runtime/graph.h"
#include "callee_graph_of.h"
#include "graph/graph_view.h"

#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace camel::graph {

/// A node of the view and a node of the frozen graph are the same integer, which is what lets this
/// adapter answer today's numbers unchanged. If that ever stops holding, every id in the view moves
/// and this is where it is caught rather than in a reader.
static_assert(
    std::is_same_v<NodeRef, camel::runtime::gc_node_ref_t>,
    "The view's NodeRef is the frozen graph's gc_node_ref_t.");

class FrozenGraphAdapter final : public GraphView {
  public:
    /// `calleeGraphOf` is how a callee referred to by value is named: the frozen graph stores a
    /// `GraphId` and its owner holds the graph (`callee_graph_of.h`). Without it the adapter still
    /// answers every other question and refuses a callee, rather than answering that a node calls
    /// nothing.
    explicit FrozenGraphAdapter(
        const camel::runtime::GCGraph &graph, CalleeGraphOf calleeGraphOf = {})
        : graph_(graph), calleeGraphOf_(std::move(calleeGraphOf)) {}

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
    /// The view's kind for a stored kind, by the table above. `Join`, `Dref` and `Dead` never reach
    /// it: `contains` refuses them first, so this function's answer for them would be a kind the
    /// view does not have and is not asked for.
    static NodeKind viewKind(camel::runtime::GCNodeKind stored);

    const camel::runtime::GCGraph &graph_;
    CalleeGraphOf calleeGraphOf_;
};

} // namespace camel::graph