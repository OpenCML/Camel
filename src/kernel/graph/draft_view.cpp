// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `DraftGraphAdapter`: the answers of `GraphView` over the graph a session is building. The header
 * (`draft_view.h`) carries the mapping table and the id table's two forms; this file is the
 * mapping.
 *
 * Two things are the draft's and not the view's, and both are answered here rather than stored
 * anywhere a reader can see:
 *
 *  - a list of ids read through the encoder's table is copied into a buffer this adapter owns (a
 *    draft stores its own numbering, the view answers the encoding's), so `translated` keeps one
 *    buffer per question and the header says how long an answer lives;
 *  - a node the draft no longer holds is a gap: `alive` refuses an erased place and the encoder's
 *    table refuses a placeholder, and neither is a node of the view.
 *
 * A CAST and a COPY became calls of `:op/cast` and `:op/copy` (plan 2.3, step 1) and are stored
 * OPER records here, so `operatorOf` answers their operator like any other node's. The two kinds
 * that are still kinds answer `NodeKind::Oper` and `kNoOper` from `operatorOf`, exactly as the
 * frozen adapter does -- those nodes carry no operator identity, which `graph_view.h` says at
 * length: it is here twice because the two representations store the same two kinds.
 */

#include "graph/draft_view.h"

#include "camel/utils/assert.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>
#include <vector>

// The draft's face, and the destructor that frees it: both are here because the adapter is what the
// draft keeps (`draft.h`, `viewCache_`). Global scope, because a member of
// `camel::runtime::GraphDraft` is defined where no namespace encloses it wrongly.
const camel::graph::GraphView &camel::runtime::GraphDraft::view() const {
    if (viewCache_ == nullptr) {
        viewCache_ = new camel::graph::DraftGraphAdapter(*this);
    }
    return *static_cast<const camel::graph::GraphView *>(viewCache_);
}

camel::runtime::GraphDraft::~GraphDraft() {
    delete static_cast<const camel::graph::GraphView *>(viewCache_);
    viewCache_ = nullptr;
}
namespace camel::graph {
namespace {

using camel::runtime::DraftNodeHeader;
using camel::runtime::GCNodeKind;
using camel::runtime::GCRegionKind;
using camel::runtime::GraphDraft;

/// The four high bits of a translation buffer's key, one value per kind of question, so that an
/// answer to one question is never overwritten by an answer to another. See `translated`.
enum class TranslatedSlot : std::uint64_t {
    Inputs = 1,
    Users  = 2,
    Region = 3,
};

std::uint64_t translatedKey(TranslatedSlot slot, std::uint64_t first, std::uint64_t second) {
    return (static_cast<std::uint64_t>(slot) << 62) | (first << 32) | second;
}

/// A node of the draft that the view has: a JOIN, the front end's placeholder and a removed place
/// are not nodes of the view (the file comment).
bool viewHasNode(const DraftNodeHeader *header) {
    if (header == nullptr) {
        return false;
    }
    switch (header->kind) {
    case GCNodeKind::Join:
    case GCNodeKind::Dref:
    case GCNodeKind::Dead:
        return false;
    default:
        return true;
    }
}

/// The payload body of `ref` copied out of the draft's tail; false when it has none or the payload
/// is smaller than `Body`. A draft keeps its payload at an arbitrary offset, so a body is never
/// reinterpreted in place.
template <typename Body> bool payloadBodyOf(const GraphDraft &draft, std::uint32_t ref, Body &out) {
    const std::span<const std::byte> bytes = draft.payloadOf(ref);
    if (bytes.size() < sizeof(Body)) {
        return false;
    }
    std::memcpy(&out, bytes.data(), sizeof(Body));
    return true;
}

} // namespace

NodeKind DraftGraphAdapter::viewKind(camel::runtime::GCNodeKind stored) {
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
        return NodeKind::Data;
    }
    return NodeKind::Data;
}

NodeRef DraftGraphAdapter::sourceOf(NodeRef ref) const {
    if (kept_.empty()) {
        return ref;
    }
    return ref < kept_.size() ? kept_[ref] : kNoNode;
}

NodeRef DraftGraphAdapter::viewIdOf(NodeRef draftId) const {
    if (kept_.empty()) {
        return draftId;
    }
    for (std::size_t i = 0; i < kept_.size(); ++i) {
        if (kept_[i] == draftId) {
            return static_cast<NodeRef>(i);
        }
    }
    return kNoNode;
}

std::vector<NodeRef> DraftGraphAdapter::viewIdsOf(std::span<const NodeRef> draftIds) const {
    std::vector<NodeRef> mapped;
    mapped.reserve(draftIds.size());
    for (const NodeRef id : draftIds) {
        mapped.push_back(viewIdOf(id));
    }
    return mapped;
}

std::span<const NodeRef>
DraftGraphAdapter::translated(std::uint64_t slot, std::span<const NodeRef> list) const {
    std::vector<NodeRef> &buffer = translated_[slot];
    buffer.clear();
    buffer.reserve(list.size());
    for (const NodeRef id : list) {
        buffer.push_back(viewIdOf(id));
    }
    return buffer;
}

camel::core::type::FunctionType *DraftGraphAdapter::funcType() const { return graph_->funcType(); }

std::vector<NodeRef> DraftGraphAdapter::ports(PortGroup group) const {
    const GraphDraft &draft = *graph_;
    switch (group) {
    case PortGroup::Normal:
        return viewIdsOf(draft.normPorts());
    case PortGroup::Context:
        return viewIdsOf(draft.withPorts());
    case PortGroup::Capture:
        return viewIdsOf(draft.closureNodes());
    default:
        return {};
    }
}

NodeRef DraftGraphAdapter::exit() const {
    // The place the draft names, if it holds a live node there -- the same rule as the frozen
    // adapter's, and not "a node of the view": the draft's own answer is the answer, and the place
    // it names may be one the view folds into another node.
    const NodeRef exit = graph_->exitNode();
    if (exit == camel::runtime::kInvalidNodeRef) {
        return kNoNode;
    }
    const NodeRef mapped = viewIdOf(exit);
    return mapped != kNoNode && holdsPlace(mapped) ? mapped : kNoNode;
}

std::string_view DraftGraphAdapter::name() const {
    // A draft is not named until it is encoded, and the name is the argument of `encode`
    // (`draft.h`). Empty is the honest answer, and `originOf` answering 0 is the precedent for it.
    return {};
}

std::uint32_t DraftGraphAdapter::nodeSlotCount() const {
    return kept_.empty() ? static_cast<std::uint32_t>(graph_->nodeSlotCount())
                         : static_cast<std::uint32_t>(kept_.size());
}

bool DraftGraphAdapter::contains(NodeRef ref) const {
    if (!holdsPlace(ref)) {
        return false;
    }
    const GraphDraft &draft = *graph_;
    return viewHasNode(draft.header(sourceOf(ref)));
}

NodeKind DraftGraphAdapter::kindOf(NodeRef ref) const {
    const GraphDraft &draft     = *graph_;
    const DraftNodeHeader *head = draft.header(sourceOf(ref));
    return viewKind(head == nullptr ? GCNodeKind::Dead : head->kind);
}

const camel::core::type::Type *DraftGraphAdapter::typeOf(NodeRef ref) const {
    const DraftNodeHeader *head = graph_->header(sourceOf(ref));
    return head == nullptr ? nullptr : head->dataType;
}

std::optional<::slot_t> DraftGraphAdapter::constantOf(NodeRef ref) const {
    const GraphDraft &draft     = *graph_;
    const DraftNodeHeader *head = draft.header(sourceOf(ref));
    if (head == nullptr || head->kind != GCNodeKind::Data || head->dataIndex >= 0) {
        return std::nullopt;
    }
    const auto index    = static_cast<std::size_t>(-head->dataIndex);
    const auto &static_ = draft.staticSlots();
    if (index >= static_.size()) {
        return std::nullopt;
    }
    return static_[index];
}

std::optional<std::uint32_t> DraftGraphAdapter::staticIndexOf(NodeRef ref) const {
    // The graph's own half of `dataIndex`: a negative one is the static area's index, and the
    // value at it is what `constantOf` answers. A non-negative one is the execution plan's slot
    // (section 13) and is not this face's.
    const GraphDraft &draft     = *graph_;
    const DraftNodeHeader *head = draft.header(sourceOf(ref));
    if (head == nullptr || head->kind != GCNodeKind::Data || head->dataIndex >= 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(-head->dataIndex);
}

std::uint32_t DraftGraphAdapter::staticSlotCount() const {
    return static_cast<std::uint32_t>(graph_->staticSlots().size());
}

std::uint64_t DraftGraphAdapter::originOf(NodeRef ref) const {
    if (!contains(ref)) {
        return 0;
    }
    return graph_->nodeOrigin(sourceOf(ref));
}

std::span<const NodeRef> DraftGraphAdapter::inputs(NodeRef ref, PortGroup group) const {
    const GraphDraft &draft = *graph_;
    // A record at the place, not `contains` (see graph_view.h): a JOIN and a DREF hold one and
    // answer their ports.
    if (!holdsPlace(ref) || draft.header(sourceOf(ref)) == nullptr) {
        return {};
    }
    const NodeRef source = sourceOf(ref);
    switch (group) {
    case PortGroup::Normal: {
        const std::span<const NodeRef> list = draft.normInputsOf(source);
        return kept_.empty() ? list
                             : translated(translatedKey(TranslatedSlot::Inputs, ref, 0), list);
    }
    case PortGroup::Context: {
        const std::span<const NodeRef> list = draft.withInputsOf(source);
        return kept_.empty() ? list
                             : translated(translatedKey(TranslatedSlot::Inputs, ref, 1), list);
    }
    default:
        // A capture is a port of the graph, not of one of its nodes (see `ports`).
        return {};
    }
}

std::span<const NodeRef> DraftGraphAdapter::users(NodeRef ref, PortGroup group) const {
    const GraphDraft &draft = *graph_;
    // The same gate `inputs` uses: a record at the place, which a JOIN has and `contains` denies.
    if (!holdsPlace(ref) || draft.header(sourceOf(ref)) == nullptr) {
        return {};
    }
    const NodeRef source = sourceOf(ref);
    switch (group) {
    case PortGroup::Normal: {
        const std::span<const NodeRef> list = draft.normUsersOf(source);
        return kept_.empty() ? list
                             : translated(translatedKey(TranslatedSlot::Users, ref, 0), list);
    }
    case PortGroup::Context: {
        const std::span<const NodeRef> list = draft.withUsersOf(source);
        return kept_.empty() ? list
                             : translated(translatedKey(TranslatedSlot::Users, ref, 1), list);
    }
    default:
        return {};
    }
}

std::vector<NodeRef> DraftGraphAdapter::predecessors(NodeRef ref, EdgeKind kind) const {
    const GraphDraft &draft = *graph_;
    std::vector<NodeRef> preds;
    // The same gate `inputs` uses: a record at the place, which a JOIN has and `contains` denies.
    if (!holdsPlace(ref) || draft.header(sourceOf(ref)) == nullptr) {
        return preds;
    }
    const NodeRef source = sourceOf(ref);
    switch (kind) {
    case EdgeKind::Data:
        for (const NodeRef in : draft.normInputsOf(source)) {
            preds.push_back(viewIdOf(in));
        }
        for (const NodeRef in : draft.withInputsOf(source)) {
            preds.push_back(viewIdOf(in));
        }
        break;
    case EdgeKind::Sync:
        for (const NodeRef in : draft.syncInputsOf(source)) {
            pushMapped(preds, in);
        }
        break;
    case EdgeKind::Order:
        for (const NodeRef in : draft.orderInputsOf(source)) {
            pushMapped(preds, in);
        }
        break;
    }
    return preds;
}

void DraftGraphAdapter::pushMapped(std::vector<NodeRef> &into, NodeRef draftId) const {
    // A side array of the draft may name a place that is not in the runtime graph (a placeholder,
    // or a node the encoding dropped): the encoding's own array has no such entry, so an entry the
    // table cannot name is not a predecessor of the view's node and is left out rather than
    // answered as "no node" -- an order or sync edge names a *node*, and one that is not there
    // names nothing.
    const NodeRef mapped = viewIdOf(draftId);
    if (mapped != kNoNode) {
        into.push_back(mapped);
    }
}

std::uint32_t DraftGraphAdapter::regionCount() const {
    return static_cast<std::uint32_t>(graph_->regionCount());
}

RegionRecord DraftGraphAdapter::region(RegionId id) const {
    RegionRecord record;
    const GraphDraft &draft = *graph_;
    if (id >= draft.regionCount()) {
        return record;
    }
    const camel::runtime::GCRegion &stored = draft.region(id);
    switch (stored.kind) {
    case GCRegionKind::Arm:
        record.kind = RegionKind::Arm;
        break;
    case GCRegionKind::LoopCond:
        record.kind = RegionKind::LoopCond;
        break;
    case GCRegionKind::LoopBody:
        record.kind = RegionKind::LoopBody;
        break;
    case GCRegionKind::Root:
    case GCRegionKind::Inline:
    case GCRegionKind::Block:
    case GCRegionKind::Dead:
        record.kind = RegionKind::Root;
        break;
    }
    record.owner =
        stored.owner == camel::runtime::kInvalidNodeRef ? kNoNode : viewIdOf(stored.owner);
    record.index = stored.index;
    // The body is in nothing; every other region is in at least the body (see the frozen adapter).
    record.parent = id == 0 ? kNoRegion : static_cast<RegionId>(stored.parent);
    record.result =
        stored.result == camel::runtime::kInvalidNodeRef ? kNoNode : viewIdOf(stored.result);
    return record;
}

std::span<const NodeRef> DraftGraphAdapter::regionParameters(RegionId id) const {
    const GraphDraft &draft = *graph_;
    if (id >= draft.regionCount()) {
        return {};
    }
    const std::span<const NodeRef> list = draft.regionPortsOf(id);
    return kept_.empty() ? list : translated(translatedKey(TranslatedSlot::Region, id, 0), list);
}

std::vector<NodeRef> DraftGraphAdapter::regionNodes(RegionId id) const {
    std::vector<NodeRef> nodes;
    for (NodeRef ref = 0; ref < nodeSlotCount(); ++ref) {
        if (contains(ref) && regionOf(ref) == id) {
            nodes.push_back(ref);
        }
    }
    return nodes;
}

RegionId DraftGraphAdapter::regionOf(NodeRef ref) const {
    // A place the draft holds, not only a node of the view: see the frozen adapter's `regionOf`.
    if (!holdsPlace(ref)) {
        return kNoRegion;
    }
    const DraftNodeHeader *head = graph_->header(sourceOf(ref));
    return head == nullptr ? kNoRegion : static_cast<RegionId>(head->region);
}

bool DraftGraphAdapter::holdsPlace(NodeRef ref) const {
    if (ref >= nodeSlotCount()) {
        return false;
    }
    const NodeRef source = sourceOf(ref);
    return source != kNoNode && graph_->alive(source);
}

bool DraftGraphAdapter::isAncestorOf(RegionId ancestor, RegionId region) const {
    if (ancestor == kNoRegion || region == kNoRegion) {
        return false;
    }
    const GraphDraft &draft = *graph_;
    for (RegionId at = region;;) {
        if (at == ancestor) {
            return true;
        }
        if (at == 0 || at >= draft.regionCount()) {
            return false;
        }
        const camel::runtime::GCRegion &one = draft.region(at);
        if (one.parent == at) {
            return false;
        }
        at = static_cast<RegionId>(one.parent);
    }
}

std::vector<RegionId> DraftGraphAdapter::regionsOwnedBy(NodeRef ref) const {
    std::vector<RegionId> owned;
    const GraphDraft &draft = *graph_;
    if (!contains(ref)) {
        return owned;
    }
    const NodeRef source        = sourceOf(ref);
    const DraftNodeHeader *head = draft.header(source);
    if (head == nullptr) {
        return owned;
    }
    switch (head->kind) {
    case GCNodeKind::Brch: {
        const std::uint32_t arms = draft.armCountOf(source);
        owned.reserve(arms);
        for (std::uint32_t index = 0; index < arms; ++index) {
            const std::uint32_t id = draft.armRegionOf(source, index);
            if (id != 0) {
                owned.push_back(static_cast<RegionId>(id));
            }
        }
        break;
    }
    case GCNodeKind::Loop: {
        const std::uint32_t cond = draft.loopRegionOf(source, GCRegionKind::LoopCond);
        const std::uint32_t body = draft.loopRegionOf(source, GCRegionKind::LoopBody);
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

CalleeRef DraftGraphAdapter::calleeOf(NodeRef ref) const {
    CalleeRef callee;
    const GraphDraft &draft = *graph_;
    if (!contains(ref)) {
        return callee;
    }
    const NodeRef source        = sourceOf(ref);
    const DraftNodeHeader *head = draft.header(source);
    if (head == nullptr || head->kind != GCNodeKind::Func) {
        return callee;
    }
    const camel::runtime::GraphId id = draft.calleeIdOf(source);
    if (id == camel::runtime::kInvalidGraphId) {
        return callee;
    }
    GUARD(
        static_cast<bool>(calleeGraphOf_),
        std::format(
            "The node {} names the callee graph {}, and this adapter was built without a way to "
            "name a graph by value. `DraftGraphAdapter(transaction, calleeGraphOf)` is the form "
            "that answers a callee (callee_graph_of.h): answering `Kind::None` here would say the "
            "node calls nothing, which is not what the draft stores.",
            ref,
            camel::runtime::toValue(id)));
    callee.kind  = CalleeRef::Kind::Value;
    callee.graph = calleeGraphOf_(id);
    if (callee.graph == nullptr) {
        callee.kind = CalleeRef::Kind::None;
    }
    return callee;
}

camel::core::OperId DraftGraphAdapter::operatorOf(NodeRef ref) const {
    const GraphDraft &draft     = *graph_;
    const DraftNodeHeader *head = draft.header(sourceOf(ref));
    // `kNoOper` for a kind that names no operator record; the four kinds the view folds into
    // `NodeKind::Oper` keep their own payload in both representations -- a CAST and a COPY none at
    // all, a FILL and an ACCS a kind and a place -- and the old face answered an empty URI for
    // them. The frozen adapter's comment says the same thing at the same place: one comment, two
    // readers of one answer.
    if (head == nullptr || head->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    camel::runtime::GCOperBody body{};
    if (!payloadBodyOf(draft, sourceOf(ref), body)) {
        return camel::core::kNoOper;
    }
    return body.oper;
}

LoopForm DraftGraphAdapter::loopFormOf(NodeRef ref) const {
    LoopForm form;
    const GraphDraft &draft     = *graph_;
    const DraftNodeHeader *head = draft.header(sourceOf(ref));
    if (head == nullptr || head->kind != GCNodeKind::Loop) {
        return form;
    }
    const camel::runtime::GCLoopBody body = draft.loopGraphsOf(sourceOf(ref));
    form.kind                             = body.kind;
    form.iterableForm                     = body.iterableForm;
    form.collectionCount                  = body.collectionCount;
    return form;
}

} // namespace camel::graph