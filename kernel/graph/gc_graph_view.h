// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * FrozenGraphAdapter: `GraphView` over a frozen graph (`GCGraph`, camel/runtime/graph.h).
 *
 * The transitional adapter stage 2.1 asks for ("a frozen graph and a draft, one adapter each, in
 * the graph lane's directory; stage 3 deletes them"). Replaces
 * `camel::compile::gir::FrozenGraphRead`
 * (`compile/include/camel/compile/gir/graph_read.h`): that class is the same idea stopped at the
 * facts the derivative lift asks, and it is deleted once its users are on this face.
 *
 * Who migrates to it: the lift (`modules/transforms/jvp.cpp`, `liftGraph`), the encoder's
 * self-check
 * (`compile/src/gir/graph_encode.cpp`, `firstDifference`), `lib/test/graph_faces.cpp`, then the
 * freeze side of every predicate the view is meant to answer once.
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
 *   GCNodeKind::Cast / Copy / Fill / Accs    NodeKind::Oper      -- see the refusal below
 *   GCNodeKind::Join / Dref / Dead           no node: `contains` is false, and the NodeRef is a gap
 *
 *   GCNode::kind                             kindOf
 *   GCNode::dataType / dataIndex             typeOf / dataIndexOf
 *   GCNode::flags (macro, constant)          isMacro / isConstant
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
 *   calleeIdOf / GCFuncRef                   calleeOf
 *   GCOperBody.oper / GCOperBody.uri         operatorOf (the URI comes from the operator table)
 *   armCountOf / loopRegionOf                regionsOwnedBy (a count is that list's size)
 *   GCGraph::exitNodeRef                     exit
 *   normPorts / withPorts / closureNodes     ports(Normal) / ports(Context) / captures
 *   closureType / staticSlots                captureType / staticArea
 *   nodeOrigin                               originOf
 *   nodePayloadBytes / nodeBodyAs            nothing: no payload bytes leave this face
 *
 * The refusal, and why it is not a fifth kind: until plan 2.3 declares `cast`, `copy`, `fill` and
 * `accs` as operators with records in the operator table, `operatorOf` on a stored node of one of
 * those four kinds would have to answer `kNoOper`. A reader handed `kNoOper` takes the "this
 * operator is not known" path -- a different answer, silently -- where a refusal stops in the one
 * place a person has to finish. `kindOf` therefore answers `NodeKind::Oper` for them (so the shape
 * of the graph is already the target's) and `operatorOf` refuses (so no reader can act on the gap).
 */

#pragma once

#include "camel/runtime/graph.h"
#include "graph/graph_view.h"

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
    explicit FrozenGraphAdapter(const camel::runtime::GCGraph &graph) : graph_(graph) {}

    camel::core::type::FunctionType *funcType() const override;
    std::vector<NodeRef> ports(PortGroup group) const override;
    std::vector<NodeRef> captures() const override;
    const camel::core::type::TupleType *captureType() const override;
    std::span<const ::slot_t> staticArea() const override;
    NodeRef exit() const override;

    std::uint32_t nodeSlotCount() const override;
    bool contains(NodeRef ref) const override;
    NodeKind kindOf(NodeRef ref) const override;
    const camel::core::type::Type *typeOf(NodeRef ref) const override;
    std::int32_t dataIndexOf(NodeRef ref) const override;
    bool isMacro(NodeRef ref) const override;
    bool isConstant(NodeRef ref) const override;
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

    camel::runtime::GraphId calleeOf(NodeRef ref) const override;
    camel::core::OperId operatorOf(NodeRef ref) const override;
    LoopForm loopFormOf(NodeRef ref) const override;

  private:
    /// The view's kind for a stored kind, by the table above. `Join`, `Dref` and `Dead` never reach
    /// it: `contains` refuses them first, so this function's answer for them would be a kind the
    /// view does not have and is not asked for.
    static NodeKind viewKind(camel::runtime::GCNodeKind stored);
    /// True for the four kinds that are operators once 2.3 declares them: `operatorOf` refuses
    /// there instead of answering `kNoOper`.
    static bool awaitsOperatorRecord(camel::runtime::GCNodeKind stored);

    const camel::runtime::GCGraph &graph_;
};

} // namespace camel::graph