// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `FrozenGraphAdapter`: the answers of `GraphView` over a frozen graph. The header
 * (`gc_graph_view.h`) carries the mapping table and the reasons; this file is the mapping.
 *
 * Three answers are not a field of the stored graph and are computed here, each one a reading of
 * what the frozen graph already stores and nothing more:
 *
 *  - the body is a region of the view (region 0) and the frozen table holds it only when the graph
 *    has another region besides it, so `regionCount` answers at least one and `region(0)` answers
 *    the body's record when the table is empty;
 *  - a region's form ports are one query here (`regionParameters`) and a slice of the graph's
 *    region-port array plus two fields of the region record there;
 *  - the nodes of a region are one query here and the region column of the nodes there, so
 *    `regionNodes` walks the column (a graph stores the region *of* a node, not the nodes of a
 *    region: the view answers a vector for that reason, and says so).
 *
 * A CAST and a COPY became calls of `:op/cast` and `:op/copy` (plan 2.3, step 1) and are stored
 * OPER records here, so `operatorOf` answers their operator like any other node's. The two kinds
 * that are still kinds -- a FILL and an ACCS -- answer `NodeKind::Oper` and `kNoOper` from
 * `operatorOf` (they carry no operator identity, which `graph_view.h` says at length), and the
 * three kinds the view does not have -- a JOIN, a draft's placeholder, a removed node -- are not
 * nodes here at all: `contains` is false and the id is a gap between two numbers.
 */

#include "graph/gc_graph_view.h"

#include "camel/utils/assert.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>
#include <vector>

// The graph's face, defined here rather than in the adapter's namespace: a member of
// `camel::runtime::GCGraph` is defined where no namespace encloses it wrongly. See
// camel/graph/graph.h, and camel/runtime/graph.h for the member this keeps.
const camel::graph::GraphView &camel::runtime::GCGraph::view() const {
    if (viewCache_ == nullptr) {
        viewCache_ = new camel::graph::FrozenGraphAdapter(*this);
    }
    return *static_cast<const camel::graph::GraphView *>(viewCache_);
}
namespace camel::graph {
namespace {

using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;

/// A node of the frozen graph that the view has: the four kinds that become operators, the JOIN,
/// the draft's placeholder and a removed place are not nodes of the view (the file comment).
bool viewHasNode(const GCNode *node) {
    if (node == nullptr) {
        return false;
    }
    switch (node->kind) {
    case GCNodeKind::Join:
    case GCNodeKind::Dref:
    case GCNodeKind::Dead:
        return false;
    default:
        return true;
    }
}

/// Whether the graph holds a node record at `ref`: the question `inputs`, `users` and
/// `predecessors` ask. It is **not** `contains` (see graph_view.h): a JOIN, a DREF and a dead place
/// hold a record and no kind, and their ports are a fact about the graph's edges that a reader
/// standing on the numbering still needs -- a cut has to carry what a join's arms hand out.
bool holdsRecord(const GCGraph &graph, NodeRef ref) {
    return ref < static_cast<NodeRef>(graph.nodeCount()) &&
           graph.node(static_cast<camel::runtime::gc_node_ref_t>(ref)) != nullptr;
}

/// The payload body of `ref` copied out of its bytes; false when the node has none or the payload
/// is smaller than `Body`. Copied rather than read in place: a payload body may hold pointers
/// (`GCOperBody`), and one read out of a span is read the same way a draft's is.
template <typename Body> bool payloadBodyOf(const GCGraph &graph, std::uint32_t ref, Body &out) {
    const std::span<const std::byte> bytes = graph.nodePayloadBytes(ref);
    if (bytes.size() < sizeof(Body)) {
        return false;
    }
    std::memcpy(&out, bytes.data(), sizeof(Body));
    return true;
}

} // namespace

NodeKind FrozenGraphAdapter::viewKind(camel::runtime::GCNodeKind stored) {
    switch (stored) {
    case GCNodeKind::Data:
        return NodeKind::Data;
    case GCNodeKind::Port:
        return NodeKind::Port;
    case GCNodeKind::Fill:
    case GCNodeKind::Accs:
    case GCNodeKind::Oper:
        return NodeKind::Oper;
    case GCNodeKind::Func:
        return NodeKind::Func;
    case GCNodeKind::Call:
        return NodeKind::Call;
    case GCNodeKind::Bind:
        return NodeKind::Bind;
    case GCNodeKind::Brch:
    case GCNodeKind::Join:
        return NodeKind::Brch;
    case GCNodeKind::Loop:
        return NodeKind::Loop;
    case GCNodeKind::Gate:
        return NodeKind::Gate;
    case GCNodeKind::Dref:
    case GCNodeKind::Dead:
        // Not asked: `contains` refuses these before any reader can name one. Answered as the kind
        // a place of the numbering is closest to, so that a caller which asked anyway gets a value
        // rather than a trap; nothing in the view treats this answer as a fact about a node.
        return NodeKind::Data;
    }
    return NodeKind::Data;
}

camel::core::type::FunctionType *FrozenGraphAdapter::funcType() const { return graph_.funcType(); }

std::vector<NodeRef> FrozenGraphAdapter::ports(PortGroup group) const {
    std::span<const camel::runtime::gc_node_ref_t> ports;
    switch (group) {
    case PortGroup::Normal:
        ports = graph_.normPorts();
        break;
    case PortGroup::Context:
        ports = graph_.withPorts();
        break;
    case PortGroup::Capture:
        ports = graph_.closureNodes();
        break;
    default:
        return {};
    }
    return std::vector<NodeRef>(ports.begin(), ports.end());
}

NodeRef FrozenGraphAdapter::exit() const {
    // The place the graph names, if it holds a node there -- the same rule as `regionOf`: the
    // answer is the graph's, and the graph may name a place the view has no node at.
    const camel::runtime::gc_node_ref_t ref = graph_.exitNodeRef();
    if (ref == camel::runtime::kInvalidNodeRef || ref >= nodeSlotCount() ||
        graph_.node(ref) == nullptr) {
        return kNoNode;
    }
    return static_cast<NodeRef>(ref);
}

std::string_view FrozenGraphAdapter::name() const { return graph_.name(); }

std::uint32_t FrozenGraphAdapter::nodeSlotCount() const {
    return static_cast<std::uint32_t>(graph_.nodeCount());
}

bool FrozenGraphAdapter::contains(NodeRef ref) const {
    if (ref >= nodeSlotCount()) {
        return false;
    }
    return viewHasNode(graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref)));
}

NodeKind FrozenGraphAdapter::kindOf(NodeRef ref) const {
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    return viewKind(node == nullptr ? GCNodeKind::Dead : node->kind);
}

const camel::core::type::Type *FrozenGraphAdapter::typeOf(NodeRef ref) const {
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    return node == nullptr ? nullptr : node->dataType();
}

std::optional<::slot_t> FrozenGraphAdapter::constantOf(NodeRef ref) const {
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    if (node == nullptr || node->kind != GCNodeKind::Data || node->dataIndex >= 0) {
        return std::nullopt;
    }
    const auto index  = static_cast<std::size_t>(-node->dataIndex);
    const auto &slots = graph_.staticSlots();
    if (index >= slots.size()) {
        return std::nullopt;
    }
    return slots[index];
}

std::optional<std::uint32_t> FrozenGraphAdapter::staticIndexOf(NodeRef ref) const {
    // The graph's own half of `dataIndex`: a negative one is the static area's index, and the
    // value at it is what `constantOf` answers. A non-negative one is the execution plan's slot
    // (section 13) and is not this face's.
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    if (node == nullptr || node->kind != GCNodeKind::Data || node->dataIndex >= 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(-node->dataIndex);
}

std::uint32_t FrozenGraphAdapter::staticSlotCount() const {
    return static_cast<std::uint32_t>(graph_.staticSlots().size());
}

std::uint64_t FrozenGraphAdapter::originOf(NodeRef ref) const {
    return graph_.nodeOrigin(static_cast<camel::runtime::gc_node_ref_t>(ref));
}

std::span<const NodeRef> FrozenGraphAdapter::inputs(NodeRef ref, PortGroup group) const {
    const camel::runtime::gc_node_ref_t node = static_cast<camel::runtime::gc_node_ref_t>(ref);
    if (!holdsRecord(graph_, ref)) {
        return {};
    }
    switch (group) {
    case PortGroup::Normal:
        return graph_.normInputsOf(node);
    case PortGroup::Context:
        return graph_.withInputsOf(node);
    default:
        // A capture is a port of the *graph*, not of one of its nodes: it is answered by
        // `ports(PortGroup::Capture)` (the field, above).
        return {};
    }
}

std::span<const NodeRef> FrozenGraphAdapter::users(NodeRef ref, PortGroup group) const {
    const camel::runtime::gc_node_ref_t node = static_cast<camel::runtime::gc_node_ref_t>(ref);
    if (!holdsRecord(graph_, ref)) {
        return {};
    }
    switch (group) {
    case PortGroup::Normal:
        return graph_.normUsersOf(node);
    case PortGroup::Context:
        return graph_.withUsersOf(node);
    default:
        return {};
    }
}

std::vector<NodeRef> FrozenGraphAdapter::predecessors(NodeRef ref, EdgeKind kind) const {
    const camel::runtime::gc_node_ref_t node = static_cast<camel::runtime::gc_node_ref_t>(ref);
    std::vector<NodeRef> preds;
    if (!holdsRecord(graph_, ref)) {
        return preds;
    }
    switch (kind) {
    case EdgeKind::Data: {
        // Both ports of the node, in port order: the ordinary group first, then the context ones,
        // which is the order the input table keeps them in.
        for (const camel::runtime::gc_node_ref_t in : graph_.normInputsOf(node)) {
            preds.push_back(in);
        }
        for (const camel::runtime::gc_node_ref_t in : graph_.withInputsOf(node)) {
            preds.push_back(in);
        }
        break;
    }
    case EdgeKind::Sync:
        for (const camel::runtime::GCGraphSyncEdge &edge : graph_.syncPredsOf(node)) {
            preds.push_back(edge.pred);
        }
        break;
    case EdgeKind::Order:
        for (const camel::runtime::GCGraphOrderEdge &edge : graph_.orderPredsOf(node)) {
            preds.push_back(edge.pred);
        }
        break;
    }
    return preds;
}

std::uint32_t FrozenGraphAdapter::regionCount() const {
    // The view's region 0 is the body, and the frozen table holds the body's record only when the
    // graph has another region: a graph whose only region is its body answers 0 there and one
    // region here (`region(0)` answers the body's record).
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(graph_.regionCount()));
}

RegionRecord FrozenGraphAdapter::region(RegionId id) const {
    RegionRecord record;
    const camel::runtime::GCRegion *stored = graph_.region(id);
    if (stored == nullptr) {
        // Region 0 when the table holds nothing but the body, and every id the graph does not have.
        return record;
    }
    switch (stored->kind) {
    case camel::runtime::GCRegionKind::Arm:
        record.kind = RegionKind::Arm;
        break;
    case camel::runtime::GCRegionKind::LoopCond:
        record.kind = RegionKind::LoopCond;
        break;
    case camel::runtime::GCRegionKind::LoopBody:
        record.kind = RegionKind::LoopBody;
        break;
    case camel::runtime::GCRegionKind::Root:
    case camel::runtime::GCRegionKind::Inline:
    case camel::runtime::GCRegionKind::Block:
    case camel::runtime::GCRegionKind::Dead:
        // The three the view does not have are read as bodies: an inlined body is an ordinary
        // region of this graph, a block is a tag set and not a place, and a removed region is a
        // tombstone. Their parent, owner and result still say where each one sits, which is all a
        // reader of this stage asks; no reader treats one as a control region (ControlRegions'
        // test is on the view's kinds, and a body is not one).
        record.kind = RegionKind::Root;
        break;
    }
    record.owner = stored->owner == camel::runtime::kInvalidNodeRef
                       ? kNoNode
                       : static_cast<NodeRef>(stored->owner);
    record.index = stored->index;
    // The body is in nothing; every other region is in at least the body, so the representation's
    // "0" is a real parent for everything but region 0 itself.
    record.parent = id == 0 ? kNoRegion : static_cast<RegionId>(stored->parent);
    record.result = stored->result == camel::runtime::kInvalidNodeRef
                        ? kNoNode
                        : static_cast<NodeRef>(stored->result);
    return record;
}

std::span<const NodeRef> FrozenGraphAdapter::regionParameters(RegionId id) const {
    return graph_.regionPorts(id);
}

std::vector<NodeRef> FrozenGraphAdapter::regionNodes(RegionId id) const {
    std::vector<NodeRef> nodes;
    for (NodeRef ref = 0; ref < nodeSlotCount(); ++ref) {
        if (contains(ref) && regionOf(ref) == id) {
            nodes.push_back(ref);
        }
    }
    return nodes;
}

RegionId FrozenGraphAdapter::regionOf(NodeRef ref) const {
    // Any place the graph holds a node at, not only a node of the view: a JOIN and a DEREF are
    // folded into the node they serve (`viewKind`) and are not nodes here, but the graph's own
    // region table still says where they run, and `decidingRegionOf` is asked about a JOIN by the
    // branch-arm invariant (kernel/runtime/branch_arms.cpp). Answering "no region" there would put
    // every JOIN in the body and redden that invariant for every branch inside an arm.
    const auto stored = static_cast<camel::runtime::gc_node_ref_t>(ref);
    if (ref >= nodeSlotCount() || graph_.node(stored) == nullptr) {
        return kNoRegion;
    }
    return static_cast<RegionId>(graph_.nodeRegion(stored));
}

bool FrozenGraphAdapter::isAncestorOf(RegionId ancestor, RegionId region) const {
    if (ancestor == kNoRegion || region == kNoRegion) {
        return false;
    }
    return graph_.isAncestorOf(
        static_cast<std::uint32_t>(ancestor),
        static_cast<std::uint32_t>(region));
}

std::vector<RegionId> FrozenGraphAdapter::regionsOwnedBy(NodeRef ref) const {
    std::vector<RegionId> owned;
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    if (node == nullptr) {
        return owned;
    }
    switch (node->kind) {
    case GCNodeKind::Brch: {
        // The arms by index: the graph stores each arm's `index` on its region, so the list is the
        // table's Arm entries for this owner, put in arm order.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> arms;
        for (std::uint32_t id = 0; id < graph_.regionCount(); ++id) {
            const camel::runtime::GCRegion *part = graph_.region(id);
            if (part != nullptr && part->kind == camel::runtime::GCRegionKind::Arm &&
                part->owner == static_cast<camel::runtime::gc_node_ref_t>(ref)) {
                arms.emplace_back(part->index, id);
            }
        }
        std::sort(arms.begin(), arms.end());
        owned.reserve(arms.size());
        for (const auto &[index, id] : arms) {
            (void)index;
            owned.push_back(static_cast<RegionId>(id));
        }
        break;
    }
    case GCNodeKind::Loop: {
        const camel::runtime::gc_node_ref_t loop = static_cast<camel::runtime::gc_node_ref_t>(ref);
        const std::uint32_t cond =
            graph_.loopRegionOf(loop, camel::runtime::GCRegionKind::LoopCond);
        const std::uint32_t body =
            graph_.loopRegionOf(loop, camel::runtime::GCRegionKind::LoopBody);
        if (cond != 0) {
            owned.push_back(static_cast<RegionId>(cond));
        }
        if (body != 0) {
            owned.push_back(static_cast<RegionId>(body));
        }
        break;
    }
    default:
        break;
    }
    return owned;
}

CalleeRef FrozenGraphAdapter::calleeOf(NodeRef ref) const {
    CalleeRef callee;
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    if (node == nullptr || node->kind != GCNodeKind::Func) {
        return callee;
    }
    const camel::runtime::GraphId id =
        graph_.calleeIdOf(static_cast<camel::runtime::gc_node_ref_t>(ref));
    if (id == camel::runtime::kInvalidGraphId) {
        // A FUNC node that names no callee yet: the view's `Kind::None`, which says the same.
        return callee;
    }
    GUARD(
        static_cast<bool>(calleeGraphOf_),
        std::format(
            "The node {} names the callee graph {}, and this adapter was built without a way to "
            "name a graph by value. `FrozenGraphAdapter(graph, calleeGraphOf)` is the form that "
            "answers a callee (callee_graph_of.h): answering `Kind::None` here would say the node "
            "calls nothing, which is not what the graph stores.",
            ref,
            camel::runtime::toValue(id)));
    callee.kind  = CalleeRef::Kind::Value;
    callee.graph = calleeGraphOf_(id);
    if (callee.graph == nullptr) {
        // The owner does not hold that graph: it names no graph this adapter can hand out.
        callee.kind = CalleeRef::Kind::None;
    }
    return callee;
}

camel::core::OperId FrozenGraphAdapter::operatorOf(NodeRef ref) const {
    const GCNode *node = graph_.node(static_cast<camel::runtime::gc_node_ref_t>(ref));
    // `kNoOper` for a kind that names no operator record: every non-OPER node, and the four kinds
    // this view folds into `NodeKind::Oper`. A CAST and a COPY keep no payload at all and a FILL
    // and an ACCS keep their own kind and a place, in both representations, so there is no operator
    // on one of them to answer -- and answering the operator that replaces it would be the guess
    // the face is not allowed to make (graph_view.h says why, and what a caller asks instead). The
    // old face answered an empty URI for them for the same reason (`operUriOf` asks a stored OPER).
    if (node == nullptr || node->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    camel::runtime::GCOperBody body{};
    if (!payloadBodyOf(graph_, static_cast<camel::runtime::gc_node_ref_t>(ref), body)) {
        return camel::core::kNoOper;
    }
    return body.oper;
}

LoopForm FrozenGraphAdapter::loopFormOf(NodeRef ref) const {
    LoopForm form;
    const camel::runtime::gc_node_ref_t node = static_cast<camel::runtime::gc_node_ref_t>(ref);
    const GCNode *stored                     = graph_.node(node);
    if (stored == nullptr || stored->kind != GCNodeKind::Loop) {
        return form;
    }
    form.kind            = graph_.loopKindOf(node);
    form.iterableForm    = graph_.loopIterableFormOf(node);
    form.collectionCount = graph_.loopCollectionCountOf(node);
    return form;
}

} // namespace camel::graph