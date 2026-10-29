// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Runtime graph draft decode layer.
 *
 * This file introduces the first graph-native rewrite staging format. The
 * implementation intentionally focuses on decode and stable storage semantics
 * first: node ids are stable, nodes own their adjacency slices, and payload
 * bytes stay close to the runtime node layout. Encode is kept conservative for
 * now and will be completed after the editing primitives land.
 */

#include "camel/runtime/draft.h"
#include "runtime/graph_build.h"

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ranges>
#include <format>
#include <stdexcept>

namespace camel::runtime {

namespace mm = camel::core::mm;

namespace {

uint32_t capacityBytesForStorageClass(DraftNodeStorageClass storageClass, size_t tailBytes) {
    switch (storageClass) {
    case DraftNodeStorageClass::Slab64:
        return 64;
    case DraftNodeStorageClass::Slab96:
        return 96;
    case DraftNodeStorageClass::Slab128:
        return 128;
    case DraftNodeStorageClass::Oversize:
        return static_cast<uint32_t>(tailBytes);
    }
    return 0;
}

size_t adjacencyBytes(
    gc_cnt_t normInputs, gc_cnt_t withInputs, gc_cnt_t ctrlInputs, gc_cnt_t orderInputs,
    gc_cnt_t syncInputs) {
    return (static_cast<size_t>(normInputs) + static_cast<size_t>(withInputs) +
            static_cast<size_t>(ctrlInputs) + static_cast<size_t>(orderInputs) +
            static_cast<size_t>(syncInputs)) *
           sizeof(gc_node_ref_t);
}

size_t growthSlackBytes(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
        return 0;
    case GCNodeKind::Brch:
    case GCNodeKind::Join:
    case GCNodeKind::Call:
    case GCNodeKind::Oper:
        return 24;
    default:
        return 12;
    }
}

camel::core::type::Type *resolveDraftNodeDataType(const GCGraph *graph, const GCNode *sourceNode) {
    ASSERT(graph != nullptr, "Draft node type resolution requires a non-null graph.");
    ASSERT(sourceNode != nullptr, "Draft node type resolution requires a non-null source node.");
    if (sourceNode->dataType() != nullptr) {
        return sourceNode->dataType();
    }

    if (sourceNode->dataIndex > 0) {
        const auto *runtimeTuple = graph->runtimeDataType();
        if (runtimeTuple != nullptr &&
            static_cast<size_t>(sourceNode->dataIndex) < runtimeTuple->size()) {
            return runtimeTuple->typeAt(static_cast<size_t>(sourceNode->dataIndex));
        }
    } else if (sourceNode->dataIndex < 0) {
        const auto *staticTuple  = graph->staticDataType();
        const size_t staticIndex = static_cast<size_t>(-sourceNode->dataIndex);
        if (staticTuple != nullptr && staticIndex < staticTuple->size()) {
            return staticTuple->typeAt(staticIndex);
        }
    }
    return nullptr;
}

DraftNodeInit makeInitFromNode(const DraftNode *node) {
    DraftNodeInit init;
    ASSERT(node != nullptr, "Draft node snapshot requires a non-null node.");
    init.dataIndex    = node->header.dataIndex;
    init.dataType     = node->header.dataType;
    init.kind         = node->header.kind;
    init.runtimeFlags = node->header.runtimeFlags;
    init.origin       = node->header.origin;
    init.permissions  = node->header.permissions;
    init.attrs        = node->header.attrs;
    init.region       = node->header.region;
    init.name         = node->header.name;
    init.payload      = DraftNodeView::payload(node);
    init.normInputs   = DraftNodeView::normInputs(node);
    init.withInputs   = DraftNodeView::withInputs(node);
    init.ctrlInputs   = DraftNodeView::ctrlInputs(node);
    init.orderInputs  = DraftNodeView::orderInputs(node);
    init.syncInputs   = DraftNodeView::syncInputs(node);
    return init;
}

void initializeNodeStorage(
    GraphDraft *owner, DraftNode *draftNode, gc_node_ref_t id, const DraftNodeInit &init,
    DraftNodeStorageClass storageClass, uint32_t capacityBytes) {
    ASSERT(draftNode != nullptr, "Draft node storage initialization requires a non-null node.");
    draftNode->header.owner        = owner;
    draftNode->header.selfId       = id;
    draftNode->header.dataIndex    = init.dataIndex;
    draftNode->header.payloadBytes = static_cast<uint32_t>(init.payload.size_bytes());
    draftNode->header.capacityBytes  = capacityBytes;
    draftNode->header.dataType       = init.dataType;
    draftNode->header.normInputCount = static_cast<gc_cnt_t>(init.normInputs.size());
    draftNode->header.withInputCount = static_cast<gc_cnt_t>(init.withInputs.size());
    draftNode->header.ctrlInputCount  = static_cast<gc_cnt_t>(init.ctrlInputs.size());
    draftNode->header.orderInputCount = static_cast<gc_cnt_t>(init.orderInputs.size());
    draftNode->header.syncInputCount  = static_cast<gc_cnt_t>(init.syncInputs.size());
    draftNode->header.kind           = init.kind;
    draftNode->header.runtimeFlags   = init.runtimeFlags;
    draftNode->header.origin         = init.origin;
    draftNode->header.permissions    = init.permissions;
    draftNode->header.attrs          = init.attrs;
    draftNode->header.name           = init.name;
    draftNode->header.storageCls     = storageClass;

    if (capacityBytes != 0) {
        ASSERT(draftNode->tail != nullptr, "Draft node storage requires a tail allocation.");
        std::memset(draftNode->tail, 0, capacityBytes);
    }
    if (!init.payload.empty()) {
        std::memcpy(
            DraftNodeView::payload(draftNode).data(),
            init.payload.data(),
            init.payload.size_bytes());
    }
    if (!init.normInputs.empty()) {
        std::ranges::copy(init.normInputs, DraftNodeView::normInputs(draftNode).begin());
    }
    if (!init.withInputs.empty()) {
        std::ranges::copy(init.withInputs, DraftNodeView::withInputs(draftNode).begin());
    }
    if (!init.ctrlInputs.empty()) {
        std::ranges::copy(init.ctrlInputs, DraftNodeView::ctrlInputs(draftNode).begin());
    }
    if (!init.orderInputs.empty()) {
        std::ranges::copy(init.orderInputs, DraftNodeView::orderInputs(draftNode).begin());
    }
    if (!init.syncInputs.empty()) {
        std::ranges::copy(init.syncInputs, DraftNodeView::syncInputs(draftNode).begin());
    }
}

std::byte *tailBytes(DraftNode *node) { return node->tail; }

const std::byte *tailBytes(const DraftNode *node) { return node->tail; }

std::span<gc_node_ref_t> edgeSpan(std::byte *base, gc_cnt_t count) {
    return {
        reinterpret_cast<gc_node_ref_t *>(base),
        static_cast<size_t>(count),
    };
}

std::span<const gc_node_ref_t> edgeSpan(const std::byte *base, gc_cnt_t count) {
    return {
        reinterpret_cast<const gc_node_ref_t *>(base),
        static_cast<size_t>(count),
    };
}

/// The byte offset of a slot's run in the tail: the payload, then the runs in the order
/// DraftEdgeSlot names (`[payload][normal ports][context ports][ctrl][order][sync]`).
size_t slotOffset(const DraftNodeHeader &header, DraftEdgeSlot slot) {
    const size_t step = sizeof(gc_node_ref_t);
    size_t at         = header.payloadBytes;
    if (slot == DraftEdgeSlot::NormalPorts) {
        return at;
    }
    at += static_cast<size_t>(header.normInputCount) * step;
    if (slot == DraftEdgeSlot::ContextPorts) {
        return at;
    }
    at += static_cast<size_t>(header.withInputCount) * step;
    if (slot == DraftEdgeSlot::Ctrl) {
        return at;
    }
    at += static_cast<size_t>(header.ctrlInputCount) * step;
    if (slot == DraftEdgeSlot::Order) {
        return at;
    }
    at += static_cast<size_t>(header.orderInputCount) * step;
    return at; // Sync, the last run.
}

/// How many edges a slot's run holds.
gc_cnt_t slotCount(const DraftNodeHeader &header, DraftEdgeSlot slot) {
    switch (slot) {
    case DraftEdgeSlot::NormalPorts:
        return header.normInputCount;
    case DraftEdgeSlot::ContextPorts:
        return header.withInputCount;
    case DraftEdgeSlot::Ctrl:
        return header.ctrlInputCount;
    case DraftEdgeSlot::Order:
        return header.orderInputCount;
    case DraftEdgeSlot::Sync:
        return header.syncInputCount;
    }
    return 0;
}

std::byte *slotBase(DraftNode *node, DraftEdgeSlot slot = DraftEdgeSlot::NormalPorts) {
    return tailBytes(node) + slotOffset(node->header, slot);
}

const std::byte *slotBase(const DraftNode *node, DraftEdgeSlot slot = DraftEdgeSlot::NormalPorts) {
    return tailBytes(node) + slotOffset(node->header, slot);
}

/// The count field of a slot in a node header, writable (appendInputEntry grows a run in place).
gc_cnt_t &slotCountRef(DraftNodeHeader &header, DraftEdgeSlot slot) {
    switch (slot) {
    case DraftEdgeSlot::NormalPorts:
        return header.normInputCount;
    case DraftEdgeSlot::ContextPorts:
        return header.withInputCount;
    case DraftEdgeSlot::Ctrl:
        return header.ctrlInputCount;
    case DraftEdgeSlot::Order:
        return header.orderInputCount;
    case DraftEdgeSlot::Sync:
        return header.syncInputCount;
    }
    return header.normInputCount;
}

/// The `DraftNodeInit` span of a slot, writable.
std::span<const gc_node_ref_t> &initSlot(DraftNodeInit &init, DraftEdgeSlot slot) {
    switch (slot) {
    case DraftEdgeSlot::NormalPorts:
        return init.normInputs;
    case DraftEdgeSlot::ContextPorts:
        return init.withInputs;
    case DraftEdgeSlot::Ctrl:
        return init.ctrlInputs;
    case DraftEdgeSlot::Order:
        return init.orderInputs;
    case DraftEdgeSlot::Sync:
        return init.syncInputs;
    }
    return init.normInputs;
}

} // namespace

std::span<std::byte> DraftNodeView::payload(DraftNode *node) {
    ASSERT(node != nullptr, "Draft payload view requires a non-null node.");
    return {tailBytes(node), node->header.payloadBytes};
}

std::span<const std::byte> DraftNodeView::payload(const DraftNode *node) {
    ASSERT(node != nullptr, "Draft payload view requires a non-null node.");
    return {tailBytes(node), node->header.payloadBytes};
}

std::span<gc_node_ref_t> DraftNodeView::normInputs(DraftNode *node) {
    return edgeSpan(slotBase(node), node->header.normInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::normInputs(const DraftNode *node) {
    return edgeSpan(slotBase(node), node->header.normInputCount);
}

std::span<gc_node_ref_t> DraftNodeView::withInputs(DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::ContextPorts), node->header.withInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::withInputs(const DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::ContextPorts), node->header.withInputCount);
}

std::span<gc_node_ref_t> DraftNodeView::ctrlInputs(DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::Ctrl), node->header.ctrlInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::ctrlInputs(const DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::Ctrl), node->header.ctrlInputCount);
}

std::span<gc_node_ref_t> DraftNodeView::orderInputs(DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::Order), node->header.orderInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::orderInputs(const DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::Order), node->header.orderInputCount);
}

std::span<gc_node_ref_t> DraftNodeView::syncInputs(DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::Sync), node->header.syncInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::syncInputs(const DraftNode *node) {
    return edgeSpan(slotBase(node, DraftEdgeSlot::Sync), node->header.syncInputCount);
}

DraftNodePool::~DraftNodePool() { clear(); }

size_t DraftNodePool::capacityForStorageClass(DraftNodeStorageClass storageClass) {
    switch (storageClass) {
    case DraftNodeStorageClass::Slab64:
        return 64;
    case DraftNodeStorageClass::Slab96:
        return 96;
    case DraftNodeStorageClass::Slab128:
        return 128;
    case DraftNodeStorageClass::Oversize:
        return 0;
    }
    return 0;
}

DraftNodeStorageClass DraftNodePool::classify(size_t tailBytes) {
    if (tailBytes <= 64) {
        return DraftNodeStorageClass::Slab64;
    }
    if (tailBytes <= 96) {
        return DraftNodeStorageClass::Slab96;
    }
    if (tailBytes <= 128) {
        return DraftNodeStorageClass::Slab128;
    }
    return DraftNodeStorageClass::Oversize;
}

std::byte *DraftNodePool::allocFromArena(SlabArena &arena) {
    const size_t blocksPerPage = std::max<size_t>(1, kPageBytes / arena.blockBytes);
    if (arena.pages.empty() || arena.cursor >= blocksPerPage) {
        arena.pages.push_back(std::make_unique<std::byte[]>(arena.blockBytes * blocksPerPage));
        arena.cursor = 0;
    }
    std::byte *base = arena.pages.back().get() + arena.cursor * arena.blockBytes;
    arena.cursor++;
    std::memset(base, 0, arena.blockBytes);
    return base;
}

DraftNode *DraftNodePool::allocNode() {
    return reinterpret_cast<DraftNode *>(allocFromArena(nodeArena_));
}

std::byte *DraftNodePool::allocTail(size_t tailBytes, DraftNodeStorageClass *storageClass) {
    const auto resolvedStorageClass = classify(tailBytes);
    if (storageClass) {
        *storageClass = resolvedStorageClass;
    }

    switch (resolvedStorageClass) {
    case DraftNodeStorageClass::Slab64:
        return allocFromArena(slab64_);
    case DraftNodeStorageClass::Slab96:
        return allocFromArena(slab96_);
    case DraftNodeStorageClass::Slab128:
        return allocFromArena(slab128_);
    case DraftNodeStorageClass::Oversize: {
        void *mem = std::malloc(tailBytes);
        if (mem == nullptr) {
            throw std::bad_alloc{};
        }
        std::memset(mem, 0, tailBytes);
        oversizeBlocks_.push_back(mem);
        return reinterpret_cast<std::byte *>(mem);
    }
    }

    throw std::logic_error("Unreachable draft node storage class.");
}

void DraftNodePool::clear() {
    nodeArena_.pages.clear();
    slab64_.pages.clear();
    slab96_.pages.clear();
    slab128_.pages.clear();
    nodeArena_.cursor = 0;
    slab64_.cursor    = 0;
    slab96_.cursor    = 0;
    slab128_.cursor   = 0;
    for (void *block : oversizeBlocks_) {
        std::free(block);
    }
    oversizeBlocks_.clear();
}

bool GraphDraft::containsNode(gc_node_ref_t id) const {
    return id < nodesById_.size() && nodesById_[id] != nullptr;
}

bool GraphDraft::containsNode(const DraftNode *nodePtr) const {
    return nodePtr != nullptr && nodePtr->header.owner == this &&
           containsNode(nodePtr->header.selfId) && nodesById_[nodePtr->header.selfId] == nodePtr;
}

bool GraphDraft::alive(gc_node_ref_t id) const { return node(id) != nullptr; }

bool GraphDraft::alive(const DraftNode *nodePtr) const { return containsNode(nodePtr); }

const DraftNodeHeader *GraphDraft::header(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? &draftNode->header : nullptr;
}

const DraftNodeHeader *GraphDraft::header(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? &nodePtr->header : nullptr;
}

gc_node_ref_t GraphDraft::nodeId(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? nodePtr->header.selfId : kInvalidNodeRef;
}

gc_node_ref_t GraphDraft::draftIdOfSourceRef(gc_node_ref_t sourceRef) const {
    if (sourceRef == kInvalidNodeRef || sourceRef >= draftIdsBySourceRef_.size()) {
        return kInvalidNodeRef;
    }
    return draftIdsBySourceRef_[sourceRef];
}

gc_node_ref_t GraphDraft::sourceRefOf(gc_node_ref_t draftId) const {
    if (draftId == kInvalidNodeRef || draftId >= sourceRefsById_.size()) {
        return kInvalidNodeRef;
    }
    return sourceRefsById_[draftId];
}

std::span<const std::byte> GraphDraft::payloadOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::payload(draftNode) : std::span<const std::byte>{};
}

std::span<const std::byte> GraphDraft::payloadOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::payload(nodePtr) : std::span<const std::byte>{};
}

gc_cnt_t GraphDraft::armCountOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    if (draftNode == nullptr || draftNode->header.kind != GCNodeKind::Brch) {
        return 0;
    }
    return DraftNodeView::payloadAs<DraftBrchPayload>(draftNode)->armCount;
}

const DraftNode *GraphDraft::node(gc_node_ref_t id) const {
    if (id >= nodesById_.size()) {
        return nullptr;
    }
    return nodesById_[id];
}

DraftNode *GraphDraft::node(gc_node_ref_t id) {
    ++mutations_;
    if (id >= nodesById_.size()) {
        return nullptr;
    }
    return nodesById_[id];
}

std::span<const gc_node_ref_t> GraphDraft::normInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::normInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::normInputsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::normInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::withInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::withInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::withInputsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::withInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::contextPortsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::withInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::contextPortsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::withInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::ctrlInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlInputsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::ctrlInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::orderInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::orderInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::orderInputsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::orderInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::syncInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::syncInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::syncInputsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::syncInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::dataPortsOf(const DraftNode *nodePtr) const {
    if (!containsNode(nodePtr)) {
        return {};
    }
    const auto ports = DraftNodeView::normInputs(nodePtr);
    if (DraftNodeView::withInputs(nodePtr).empty()) {
        return ports;
    }
    // The ports are stored normal first, context second, and are contiguous: the frozen graph
    // stores one data run with the same role boundary (GCNode::inBegin .. withBegin .. ctrlBegin).
    const auto all = edgeSpan(slotBase(nodePtr), nodePtr->header.normInputCount +
                                                    nodePtr->header.withInputCount);
    ASSERT(all.data() == ports.data(), "The data port runs of a draft node are not contiguous.");
    (void)ports;
    return all;
}

std::span<const gc_node_ref_t> GraphDraft::dataPortsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? dataPortsOf(draftNode) : std::span<const gc_node_ref_t>{};
}

PortRole GraphDraft::portRoleOf(const DraftNode *nodePtr, size_t port) const {
    if (!containsNode(nodePtr)) {
        return PortRole::Normal;
    }
    return port < nodePtr->header.normInputCount ? PortRole::Normal : PortRole::Context;
}

PortRole GraphDraft::portRoleOf(gc_node_ref_t id, size_t port) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? portRoleOf(draftNode, port) : PortRole::Normal;
}

std::span<const gc_node_ref_t> GraphDraft::usersOf(gc_node_ref_t id, DraftEdgeSlot slot) const {
    if (id >= users_.size() || !users_[id]) {
        return {};
    }
    return users_[id]->of(slot);
}

std::vector<gc_node_ref_t> &GraphDraft::userListOf(gc_node_ref_t id, DraftEdgeSlot slot) {
    ++mutations_;
    if (users_.size() <= id) {
        users_.resize(nodesById_.size());
    }
    if (!users_[id]) {
        users_[id] = std::make_unique<DraftUserLists>();
    }
    return users_[id]->of(slot);
}

std::span<const gc_node_ref_t> GraphDraft::normUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeSlot::NormalPorts);
}

std::span<const gc_node_ref_t> GraphDraft::normUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeSlot::NormalPorts)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::withUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeSlot::ContextPorts);
}

std::span<const gc_node_ref_t> GraphDraft::withUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeSlot::ContextPorts)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeSlot::Ctrl);
}

std::span<const gc_node_ref_t> GraphDraft::ctrlUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeSlot::Ctrl)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::orderUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeSlot::Order);
}

std::span<const gc_node_ref_t> GraphDraft::orderUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeSlot::Order)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::syncUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeSlot::Sync);
}

std::span<const gc_node_ref_t> GraphDraft::syncUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeSlot::Sync)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t>
GraphDraft::inputsOf(gc_node_ref_t id, DraftEdgeKind kind, PortRole role) const {
    switch (edgeSlotOf(kind, role)) {
    case DraftEdgeSlot::NormalPorts:
        return normInputsOf(id);
    case DraftEdgeSlot::ContextPorts:
        return withInputsOf(id);
    case DraftEdgeSlot::Ctrl:
        return ctrlInputsOf(id);
    case DraftEdgeSlot::Order:
        return orderInputsOf(id);
    case DraftEdgeSlot::Sync:
        return syncInputsOf(id);
    }
    return {};
}

void GraphDraft::setInputs(
    gc_node_ref_t id, DraftEdgeKind kind, PortRole role, std::span<const gc_node_ref_t> inputs) {
    switch (edgeSlotOf(kind, role)) {
    case DraftEdgeSlot::NormalPorts:
        setNormInputs(id, inputs);
        return;
    case DraftEdgeSlot::ContextPorts:
        setWithInputs(id, inputs);
        return;
    case DraftEdgeSlot::Ctrl:
        setCtrlInputs(id, inputs);
        return;
    case DraftEdgeSlot::Order:
        setOrderInputs(id, inputs);
        return;
    case DraftEdgeSlot::Sync:
        setSyncInputs(id, inputs);
        return;
    }
}

std::span<gc_node_ref_t> GraphDraft::mutableInputs(DraftNode *node, DraftEdgeSlot slot) {
    ++mutations_;
    switch (slot) {
    case DraftEdgeSlot::NormalPorts:
        return DraftNodeView::normInputs(node);
    case DraftEdgeSlot::ContextPorts:
        return DraftNodeView::withInputs(node);
    case DraftEdgeSlot::Ctrl:
        return DraftNodeView::ctrlInputs(node);
    case DraftEdgeSlot::Order:
        return DraftNodeView::orderInputs(node);
    case DraftEdgeSlot::Sync:
        return DraftNodeView::syncInputs(node);
    }
    return DraftNodeView::normInputs(node);
}

bool GraphDraft::isControlAnchor(gc_node_ref_t id) const {
    const DraftNodeHeader *draftHeader = header(id);
    if (!draftHeader) {
        return false;
    }
    switch (draftHeader->kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
        return false;
    default:
        return true;
    }
}

bool GraphDraft::isControlAnchor(const DraftNode *nodePtr) const {
    return isControlAnchor(nodeId(nodePtr));
}

gc_node_ref_t GraphDraft::resolveForwardedValueRef(gc_node_ref_t id) const {
    gc_node_ref_t current = id;
    while (current != kInvalidNodeRef) {
        const DraftNodeHeader *draftHeader = header(current);
        if (!draftHeader) {
            return kInvalidNodeRef;
        }
        if (draftHeader->kind != GCNodeKind::Gate) {
            return current;
        }
        if (!ctrlInputsOf(current).empty()) {
            // A gate with control predecessors is an observable completion
            // anchor, not a transparent value-only forwarding node.
            return current;
        }
        const auto normInputs = normInputsOf(current);
        if (!normInputs.empty()) {
            current = normInputs.back();
            continue;
        }
        const auto withInputs = withInputsOf(current);
        if (!withInputs.empty()) {
            current = withInputs.back();
            continue;
        }
        return kInvalidNodeRef;
    }
    return kInvalidNodeRef;
}

gc_node_ref_t GraphDraft::resolveForwardedCtrlRef(gc_node_ref_t id) const {
    gc_node_ref_t current = id;
    while (current != kInvalidNodeRef) {
        if (alive(current)) {
            return current;
        }
        return kInvalidNodeRef;
    }
    return kInvalidNodeRef;
}

gc_node_ref_t GraphDraft::translateNodeRef(
    gc_node_ref_t sourceRef, const std::vector<gc_node_ref_t> &sourceToDraft) {
    if (sourceRef == kInvalidNodeRef) {
        return kInvalidNodeRef;
    }
    ASSERT(sourceRef < sourceToDraft.size(), "Source node ref is out of decode map range.");
    const gc_node_ref_t translated = sourceToDraft[sourceRef];
    ASSERT(translated != kInvalidNodeRef, "Source node ref was not mapped during draft decode.");
    return translated;
}

size_t GraphDraft::payloadBytesForSourceNode(const GCGraph *graph, gc_node_ref_t sourceRef) {
    const GCNode *sourceNode = graph->node(sourceRef);
    ASSERT(sourceNode != nullptr, "Draft decode encountered a null runtime node.");
    if (sourceNode->kind == GCNodeKind::Brch) {
        return sizeof(DraftBrchPayload);
    }
    return graph->nodePayloadBytes(sourceRef).size();
}

DraftNode *GraphDraft::createDecodedNode(
    const GCGraph *graph, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &sourceToDraft) {
    ++mutations_;
    const GCNode *sourceNode = graph->node(sourceRef);
    ASSERT(sourceNode != nullptr, "Draft decode encountered a null source node.");

    const auto normInputs  = graph->normInputsOf(sourceRef);
    const auto withInputs  = graph->withInputsOf(sourceRef);
    const auto ctrlInputs  = graph->ctrlInputsOf(sourceRef);

    // Order edges are a side array of the graph (GCGraph::orderPredsOf); a draft holds them in
    // the tail of the user, like its other predecessors.
    const auto orderEdges = graph->orderPredsOf(sourceRef);
    std::vector<gc_node_ref_t> orderInputs;
    orderInputs.reserve(orderEdges.size());
    for (const GCGraphOrderEdge &edge : orderEdges) {
        orderInputs.push_back(translateNodeRef(edge.pred, sourceToDraft));
    }
    // Sync edges are a side array too (GCGraph::syncPredsOf): they carry "used", so they are not
    // ports, but a draft still holds them in the tail of the user.
    const auto syncEdges = graph->syncPredsOf(sourceRef);
    std::vector<gc_node_ref_t> syncInputs;
    syncInputs.reserve(syncEdges.size());
    for (const GCGraphSyncEdge &edge : syncEdges) {
        syncInputs.push_back(translateNodeRef(edge.pred, sourceToDraft));
    }

    const uint32_t payloadBytes =
        static_cast<uint32_t>(payloadBytesForSourceNode(graph, sourceRef));
    const size_t tailBytes =
        static_cast<size_t>(payloadBytes) + adjacencyBytes(
                                                static_cast<gc_cnt_t>(normInputs.size()),
                                                static_cast<gc_cnt_t>(withInputs.size()),
                                                static_cast<gc_cnt_t>(ctrlInputs.size()),
                                                static_cast<gc_cnt_t>(orderInputs.size()),
                                                static_cast<gc_cnt_t>(syncInputs.size()));

    DraftNodeStorageClass storageClass = DraftNodeStorageClass::Oversize;
    DraftNode *draftNode               = pool_.allocNode();
    draftNode->tail                    = pool_.allocTail(tailBytes, &storageClass);
    draftNode->header.owner            = this;
    draftNode->header.selfId           = sourceToDraft[sourceRef];
    draftNode->header.dataIndex        = sourceNode->dataIndex;
    draftNode->header.payloadBytes     = payloadBytes;
    draftNode->header.capacityBytes    = capacityBytesForStorageClass(storageClass, tailBytes);
    draftNode->header.dataType         = resolveDraftNodeDataType(graph, sourceNode);
    draftNode->header.normInputCount   = static_cast<gc_cnt_t>(normInputs.size());
    draftNode->header.withInputCount   = static_cast<gc_cnt_t>(withInputs.size());
    draftNode->header.ctrlInputCount   = static_cast<gc_cnt_t>(ctrlInputs.size());
    draftNode->header.orderInputCount  = static_cast<gc_cnt_t>(orderInputs.size());
    draftNode->header.syncInputCount   = static_cast<gc_cnt_t>(syncInputs.size());
    draftNode->header.kind             = sourceNode->kind;
    draftNode->header.runtimeFlags     = sourceNode->flags;
    draftNode->header.origin           = graph->nodeOrigin(sourceRef);
    draftNode->header.permissions      = graph->nodePermissions(sourceRef);
    draftNode->header.attrs            = graph->nodeAttrs(sourceRef);
    draftNode->header.region           = graph->nodeRegion(sourceRef);
    draftNode->header.name             = graph->nodeNameSym(sourceRef);
    draftNode->header.storageCls       = storageClass;

    if (sourceNode->kind == GCNodeKind::Brch) {
        auto *payload                 = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
        const auto *sourceBrchPayload = graph->nodeBodyAs<GCBrchBody>(sourceRef);
        payload->join                 = translateNodeRef(sourceBrchPayload->join, sourceToDraft);
        payload->armCount             = sourceBrchPayload->armCount;
    } else if (const auto body = graph->nodePayloadBytes(sourceRef); !body.empty()) {
        std::memcpy(DraftNodeView::payload(draftNode).data(), body.data(), body.size());
        if (sourceNode->kind == GCNodeKind::Join) {
            auto *payload = DraftNodeView::payloadAs<GCJoinBody>(draftNode);
            payload->brch = translateNodeRef(payload->brch, sourceToDraft);
        }
    }

    auto copyTranslated = [&](std::span<gc_node_ref_t> out, std::span<const gc_node_ref_t> in) {
        for (size_t i = 0; i < in.size(); ++i) {
            out[i] = translateNodeRef(in[i], sourceToDraft);
        }
    };
    copyTranslated(DraftNodeView::normInputs(draftNode), normInputs);
    copyTranslated(DraftNodeView::withInputs(draftNode), withInputs);
    copyTranslated(DraftNodeView::ctrlInputs(draftNode), ctrlInputs);
    copyTranslated(DraftNodeView::orderInputs(draftNode), orderInputs);
    copyTranslated(DraftNodeView::syncInputs(draftNode), syncInputs);

    return draftNode;
}

std::unique_ptr<GraphDraft> GraphDraft::decode(const Program &program, const GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph draft decode requires a non-null graph.");
    CAMEL_LOG_INFO_S(
        "GraphDraft",
        "Decode begin for graph '{}' with {} nodes.",
        graph->name(),
        graph->nodeCount());
    auto draft       = std::make_unique<GraphDraft>();
    draft->id_       = graph->id();
    draft->outerGraphId_           = graph->outerGraphId();
    draft->program_                = &program;
    draft->typeSpecializationRoot_ = graph->typeSpecializationRoot();
    draft->flags_                  = graph->flags();
    draft->implKernelUri_          = graph->implKernelUri();
    draft->declarationFacts_       = graph->declarationFacts();
    draft->funcType_ = graph->funcType();
    draft->captureNames_.assign(graph->captureNames().begin(), graph->captureNames().end());
    if (const auto *layout = graph->runtimeDataType()) {
        draft->setRuntimeSlotTypes(layout->types());
    }
    draft->closureType_ = const_cast<camel::core::type::TupleType *>(graph->closureType());
    draft->returnKind_  = graph->returnKind();
    draft->staticSlots_.assign(graph->staticSlots().begin(), graph->staticSlots().end());
    if (const auto *staticType = graph->staticDataType()) {
        draft->staticSlotTypes_.assign(staticType->types().begin(), staticType->types().end());
    } else {
        draft->staticSlotTypes_.clear();
    }
    // Views of the reference table by role, built once each.
    draft->payloadGraphs_.assign(graph->refs().begin(), graph->refs().end());
    for (uint32_t i = 0; i < draft->payloadGraphs_.size(); ++i) {
        draft->payloadGraphIndex_.emplace(draft->payloadGraphs_[i], i);
    }
    draft->dependencies_    = graph->dependencies();
    draft->subGraphs_       = graph->subGraphs();
    draft->staticGraphRefs_ = graph->staticGraphRefs();
    draft->ruleGraphs_      = graph->ruleGraphs();
    draft->draftIdsBySourceRef_.assign(graph->nodeCount(), kInvalidNodeRef);

    std::vector<gc_node_ref_t> sourceToDraft(graph->nodeCount(), kInvalidNodeRef);
    draft->nodesById_.reserve(graph->nodeCount());
    draft->sourceRefsById_.reserve(graph->nodeCount());
    draft->decoded_ = true;
    draft->base_    = graph;
    for (gc_node_ref_t sourceRef = 0; sourceRef < graph->nodeCount(); ++sourceRef) {
        ASSERT(sourceRef < sourceToDraft.size(), "Runtime node ref is out of draft decode range.");
        const gc_node_ref_t draftId = static_cast<gc_node_ref_t>(draft->nodesById_.size());
        sourceToDraft[sourceRef]    = draftId;
        draft->draftIdsBySourceRef_[sourceRef] = draftId;
        draft->nodesById_.push_back(nullptr);
        draft->sourceRefsById_.push_back(sourceRef);
    }

    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const gc_node_ref_t sourceRef = it.ref();
        const gc_node_ref_t draftId   = sourceToDraft[sourceRef];
        CAMEL_LOG_INFO_S(
            "GraphDraft",
            "Decode node graph='{}' sourceRef={} draftId={} kind={}.",
            graph->name(),
            sourceRef,
            draftId,
            static_cast<int>((*it)->kind));
        draft->nodesById_[draftId] = draft->createDecodedNode(graph, sourceRef, sourceToDraft);
        // The users of a node are those the frozen graph's use index lists, in its order.
        const auto decodeUsers = [&](DraftEdgeKind kind, std::span<const gc_node_ref_t> users,
                                     PortRole role = PortRole::Normal) {
            if (users.empty()) {
                return;
            }
            auto &list = draft->userListOf(draftId, edgeSlotOf(kind, role));
            list.reserve(users.size());
            for (gc_node_ref_t user : users) {
                list.push_back(translateNodeRef(user, sourceToDraft));
            }
        };
        decodeUsers(DraftEdgeKind::Data, graph->normOutputsOf(sourceRef));
        decodeUsers(DraftEdgeKind::Data, graph->withOutputsOf(sourceRef), PortRole::Context);
        decodeUsers(DraftEdgeKind::Ctrl, graph->ctrlOutputsOf(sourceRef));
        draft->noteNodeKind(draftId, draft->nodesById_[draftId]->header.kind);
        draft->liveNodeCount_++;
    }

    // Order edges are not in the input runs: their users are read from the graph's own array,
    // in one pass, after every node has a draft id to translate to.
    if (graph->hasOrderEdges()) {
        for (const GCGraphOrderEdge &edge : graph->orderEdges()) {
            const gc_node_ref_t pred = translateNodeRef(edge.pred, sourceToDraft);
            const gc_node_ref_t user = translateNodeRef(edge.user, sourceToDraft);
            if (pred != kInvalidNodeRef && user != kInvalidNodeRef) {
                draft->userListOf(pred, DraftEdgeSlot::Order).push_back(user);
            }
        }
    }
    // The sync edges are read from their own array as well; they carry "used", but they are not
    // ports either (docs/design/effects/edges.cn.md 1.5).
    if (graph->hasSyncEdges()) {
        for (const GCGraphSyncEdge &edge : graph->syncEdges()) {
            const gc_node_ref_t pred = translateNodeRef(edge.pred, sourceToDraft);
            const gc_node_ref_t user = translateNodeRef(edge.user, sourceToDraft);
            if (pred != kInvalidNodeRef && user != kInvalidNodeRef) {
                draft->userListOf(pred, DraftEdgeSlot::Sync).push_back(user);
            }
        }
    }

    auto translateVector = [&](std::span<const gc_node_ref_t> sourceRefs,
                               std::vector<gc_node_ref_t> &target) {
        target.clear();
        target.reserve(sourceRefs.size());
        for (gc_node_ref_t ref : sourceRefs) {
            target.push_back(translateNodeRef(ref, sourceToDraft));
        }
    };

    // The regions, with their node references in the draft's numbering.
    if (graph->regionCount() != 0) {
        draft->regions_.assign(graph->nodePayload()->regionTable,
                               graph->nodePayload()->regionTable + graph->regionCount());
        for (GCRegion &region : draft->regions_) {
            region.owner    = translateNodeRef(region.owner, sourceToDraft);
            region.result   = translateNodeRef(region.result, sourceToDraft);
            region.callSite = translateNodeRef(region.callSite, sourceToDraft);
        }
        const auto* ports = graph->nodePayload()->regionPorts;
        for (size_t i = 0; i < graph->nodePayload()->regionPortCount; ++i) {
            draft->regionPorts_.push_back(translateNodeRef(ports[i], sourceToDraft));
        }
    }
    for (WritingUnion entry : graph->writings()) {
        entry.a = translateNodeRef(entry.a, sourceToDraft);
        entry.b = translateNodeRef(entry.b, sourceToDraft);
        draft->writings_.push_back(entry);
    }
    translateVector(graph->normPorts(), draft->normPorts_);
    translateVector(graph->withPorts(), draft->withPorts_);
    translateVector(graph->closureNodes(), draft->closureNodes_);

    draft->entry_      = translateNodeRef(graph->entryNodeRef(), sourceToDraft);
    draft->exit_       = translateNodeRef(graph->exitNodeRef(), sourceToDraft);
    draft->output_     = translateNodeRef(graph->outputNodeRef(), sourceToDraft);
    draft->returnNode_ = translateNodeRef(graph->returnNodeRef(), sourceToDraft);
    CAMEL_LOG_INFO_S("GraphDraft", "Decode finished for graph '{}'.", graph->name());
    draft->mutations_ = 0;
    return draft;
}

DraftNode *GraphDraft::rebuildNode(gc_node_ref_t id, const DraftNodeInit &init) {
    ++mutations_;
    const size_t tailBytes =
        init.payload.size_bytes() + adjacencyBytes(
                                        static_cast<gc_cnt_t>(init.normInputs.size()),
                                        static_cast<gc_cnt_t>(init.withInputs.size()),
                                        static_cast<gc_cnt_t>(init.ctrlInputs.size()),
                                        static_cast<gc_cnt_t>(init.orderInputs.size()),
                                        static_cast<gc_cnt_t>(init.syncInputs.size()));

    DraftNode *existing = node(id);
    if (existing != nullptr && tailBytes <= existing->header.capacityBytes) {
        // `init` is often assembled from the current node through `makeInitFromNode`.
        // When we rebuild in place, its payload and adjacency spans may alias the same
        // tail block we are about to reinitialize. Snapshot them first so structured
        // payloads such as JOIN/BRCH bodies survive in-place mutations.
        std::vector<std::byte> payloadCopy(init.payload.begin(), init.payload.end());
        std::vector<gc_node_ref_t> normInputsCopy(init.normInputs.begin(), init.normInputs.end());
        std::vector<gc_node_ref_t> withInputsCopy(init.withInputs.begin(), init.withInputs.end());
        std::vector<gc_node_ref_t> ctrlInputsCopy(init.ctrlInputs.begin(), init.ctrlInputs.end());
        std::vector<gc_node_ref_t> orderInputsCopy(init.orderInputs.begin(), init.orderInputs.end());
        std::vector<gc_node_ref_t> syncInputsCopy(init.syncInputs.begin(), init.syncInputs.end());
        DraftNodeInit copiedInit{
            .dataIndex    = init.dataIndex,
            .dataType     = init.dataType,
            .kind         = init.kind,
            .runtimeFlags = init.runtimeFlags,
            .origin       = init.origin,
            .permissions  = init.permissions,
            .attrs        = init.attrs,
            .name         = init.name,
            .payload      = payloadCopy,
            .normInputs   = normInputsCopy,
            .withInputs   = withInputsCopy,
            .ctrlInputs   = ctrlInputsCopy,
            .orderInputs  = orderInputsCopy,
            .syncInputs   = syncInputsCopy,
        };
        initializeNodeStorage(
            this,
            existing,
            id,
            copiedInit,
            existing->header.storageCls,
            existing->header.capacityBytes);
        return existing;
    }
    // A node that has outgrown its storage once is likely to grow again: a block that holds many
    // inputs doubles, so that adding them one by one costs the inputs and not their square (the
    // old block stays with the pool until the draft goes).
    size_t reservedTailBytes = tailBytes + growthSlackBytes(init.kind);
    if (existing != nullptr && existing->header.capacityBytes >= 128) {
        reservedTailBytes = std::max<size_t>(reservedTailBytes, 2 * existing->header.capacityBytes);
    }
    DraftNodeStorageClass storageClass = DraftNodeStorageClass::Oversize;
    DraftNode *draftNode               = existing != nullptr ? existing : pool_.allocNode();
    draftNode->tail                    = pool_.allocTail(reservedTailBytes, &storageClass);
    initializeNodeStorage(
        this,
        draftNode,
        id,
        init,
        storageClass,
        capacityBytesForStorageClass(storageClass, reservedTailBytes));
    return draftNode;
}

void GraphDraft::replaceNodeStorage(gc_node_ref_t id, DraftNode *nodePtr) {
    ++mutations_;
    ASSERT(id < nodesById_.size(), "Draft node storage replacement id is out of range.");
    ASSERT(nodePtr != nullptr, "Draft node storage replacement requires a non-null node.");
    nodePtr->header.owner  = this;
    nodePtr->header.selfId = id;
    nodesById_[id]         = nodePtr;
}

void GraphDraft::removeUserRef(gc_node_ref_t id, gc_node_ref_t userId, DraftEdgeSlot slot) {
    ++mutations_;
    if (node(id) == nullptr || id >= users_.size() || !users_[id]) {
        return;
    }
    DraftUserLists &lists = *users_[id];
    const auto eraseOne = [&](DraftEdgeSlot which) {
        auto &items = lists.of(which);
        if (auto *place = lists.index(which)) {
            const auto found = place->find(userId);
            if (found == place->end()) {
                return;
            }
            const uint32_t at = found->second;
            place->erase(found);
            if (at + 1 != items.size()) {
                items[at]              = items.back();
                (*place)[items[at]]    = at;
            }
            items.pop_back();
            return;
        }
        // A short list keeps its order; the user linked last is the one most often unlinked first.
        auto it = std::find(items.rbegin(), items.rend(), userId);
        if (it != items.rend()) {
            items.erase(std::next(it).base());
        }
    };
    eraseOne(slot);
}

void GraphDraft::appendUserRef(gc_node_ref_t id, gc_node_ref_t userId, DraftEdgeSlot slot) {
    ++mutations_;
    if (node(id) == nullptr) {
        return;
    }
    inheritRegion(id, userId);
    const auto appendUnique = [&](DraftEdgeSlot which) {
        auto &items = userListOf(id, which);
        DraftUserLists &lists = *users_[id];
        if (!items.empty() && items.back() == userId) {
            return;
        }
        if (auto *place = lists.index(which)) {
            if (place->emplace(userId, static_cast<uint32_t>(items.size())).second) {
                items.push_back(userId);
            }
            return;
        }
        if (std::find(items.begin(), items.end(), userId) == items.end()) {
            items.push_back(userId);
        }
    };
    appendUnique(slot);
}

uint64_t GraphDraft::nodeOrigin(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    return h ? h->origin : 0;
}

uint64_t GraphDraft::firstNodeOrigin() const {
    for (gc_node_ref_t id = 0; id < nodesById_.size(); ++id) {
        if (const DraftNodeHeader *h = header(id); h && h->origin != 0) {
            return h->origin;
        }
    }
    return 0;
}

void GraphDraft::setWritings(std::vector<WritingUnion> log) {
    if (log != writings_) {
        ++mutations_;
        writings_ = std::move(log);
    }
}

void GraphDraft::refuseWithWritings(const char *what) const {
    throw std::logic_error(
        std::string("GraphDraft::") + what +
        ": the draft has values with several writings; only nodes can be added until they are "
        "extracted.");
}

camel::core::sem::PermissionMask GraphDraft::nodePermissions(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    return h ? h->permissions : 0;
}

void GraphDraft::setNodePermissions(gc_node_ref_t id, camel::core::sem::PermissionMask permissions) {
    requireOneWriting("setNodePermissions");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set the permissions of a missing draft node.");
    draftNode->header.permissions = permissions;
}

uint32_t GraphDraft::nodeAttrs(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    return h ? h->attrs : 0;
}

void GraphDraft::setNodeAttrs(gc_node_ref_t id, uint32_t attrs) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set the attributes of a missing draft node.");
    draftNode->header.attrs = attrs;
}

uint32_t GraphDraft::nodeName(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    return h ? h->name : 0;
}

void GraphDraft::setNodeName(gc_node_ref_t id, uint32_t name) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set the name of a missing draft node.");
    draftNode->header.name = name;
}

void GraphDraft::setNodeOrigin(gc_node_ref_t id, uint64_t origin) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set the origin of a missing draft node.");
    draftNode->header.origin = origin;
}

gc_node_ref_t GraphDraft::addNode(const DraftNodeInit &init) {
    ++mutations_;
    ASSERT(
        nodesById_.size() < std::numeric_limits<gc_node_ref_t>::max(),
        "A draft graph has no node id left (the last id is kInvalidNodeRef).");
    gc_node_ref_t id = static_cast<gc_node_ref_t>(nodesById_.size());
    touchRefs();
    nodesById_.push_back(rebuildNode(id, init));
    // Constants and ports are not executed: they belong to the body, whatever arm is being written.
    const bool bodyKind = init.kind == GCNodeKind::Data || init.kind == GCNodeKind::Port;
    nodesById_[id]->header.region = init.region != 0 ? init.region : bodyKind ? 0 : insertRegion_;
    sourceRefsById_.push_back(kInvalidNodeRef);
    noteNodeKind(id, init.kind);
    liveNodeCount_++;

    for (gc_node_ref_t input : init.normInputs) {
        appendUserRef(input, id, DraftEdgeSlot::NormalPorts);
    }
    for (gc_node_ref_t input : init.withInputs) {
        appendUserRef(input, id, DraftEdgeSlot::ContextPorts);
    }
    for (gc_node_ref_t input : init.ctrlInputs) {
        appendUserRef(input, id, DraftEdgeSlot::Ctrl);
    }
    for (gc_node_ref_t input : init.orderInputs) {
        appendUserRef(input, id, DraftEdgeSlot::Order);
    }
    for (gc_node_ref_t input : init.syncInputs) {
        appendUserRef(input, id, DraftEdgeSlot::Sync);
    }
    return id;
}

void GraphDraft::noteNodeKind(gc_node_ref_t id, GCNodeKind kind) {
    ++mutations_;
    switch (kind) {
    case GCNodeKind::Gate:
        gateIds_.push_back(id);
        break;
    case GCNodeKind::Brch:
        brchIds_.push_back(id);
        break;
    case GCNodeKind::Join:
        joinIds_.push_back(id);
        break;
    default:
        break;
    }
}

void GraphDraft::appendInputEntry(gc_node_ref_t userId, DraftEdgeSlot slot, gc_node_ref_t inputId) {
    ++mutations_;
    DraftNode *draftNode = node(userId);
    DraftNodeHeader &h   = draftNode->header;
    if (h.usedBytes() + sizeof(gc_node_ref_t) > h.capacityBytes) {
        // No room: lay the node out again with the new input (the block grows by doubling).
        DraftNodeInit init = makeInitFromNode(draftNode);
        std::vector<gc_node_ref_t> grown;
        std::span<const gc_node_ref_t> current = initSlot(init, slot);
        grown.assign(current.begin(), current.end());
        grown.push_back(inputId);
        initSlot(init, slot) = grown;
        replaceNodeStorage(userId, rebuildNode(userId, init));
        return;
    }
    // Room in the block: the entry goes at the end of its run, and the runs after it move up.
    auto *base = reinterpret_cast<gc_node_ref_t *>(draftNode->tail + h.payloadBytes);
    // `slotOffset` is in bytes; `base` counts entries, and so does the memmove below. The new
    // entry goes after the ones the run already holds: a run keeps the order it was written in
    // (`(x, x)` and the port order of a call both depend on it).
    const size_t at = (slotOffset(h, slot) - h.payloadBytes) / sizeof(gc_node_ref_t) +
                      static_cast<size_t>(slotCount(h, slot));
    const size_t total = static_cast<size_t>(h.normInputCount) + h.withInputCount +
                         h.ctrlInputCount + h.orderInputCount + h.syncInputCount;
    std::memmove(base + at + 1, base + at, (total - at) * sizeof(gc_node_ref_t));
    base[at] = inputId;
    slotCountRef(h, slot)++;
}

uint32_t controlRegionOf(const GraphDraft &draft, gc_node_ref_t id) {
    uint32_t region = draft.nodeRegion(id);
    while (region != 0) {
        const GCRegion &enclosing = draft.region(region);
        if (enclosing.kind == GCRegionKind::Arm || enclosing.kind == GCRegionKind::LoopCond ||
            enclosing.kind == GCRegionKind::LoopBody) {
            return region;
        }
        region = enclosing.parent;
    }
    return 0;
}

/// The region a node's code is written in, with the region a splice gave the code it moved (Inline)
/// looked through: that code is the code of the block it was moved into (docs/design/effects/
/// edges.cn.md 5.7), so the region annotating it is not a block of its own. Nothing else is looked
/// through: a region that holds code of its own -- an arm, a part of a loop -- separates blocks.
uint32_t statementRegionOf(const GraphDraft &draft, gc_node_ref_t id) {
    uint32_t region = draft.nodeRegion(id);
    while (region != 0 && draft.region(region).kind == GCRegionKind::Inline) {
        region = draft.region(region).parent;
    }
    return region;
}

bool sameSyncBlock(const GraphDraft &draft, gc_node_ref_t a, gc_node_ref_t b) {
    return statementRegionOf(draft, a) == statementRegionOf(draft, b);
}

void GraphDraft::closeChainOver(gc_node_ref_t id) {
    requireOneWriting("closeChainOver");
    ++mutations_;
    const std::vector<gc_node_ref_t> syncInputs(syncInputsOf(id).begin(), syncInputsOf(id).end());
    const std::vector<gc_node_ref_t> ctrlInputs(ctrlInputsOf(id).begin(), ctrlInputsOf(id).end());
    const std::vector<gc_node_ref_t> syncUsers(syncUsersOf(id).begin(), syncUsersOf(id).end());
    const std::vector<gc_node_ref_t> ctrlUsers(ctrlUsersOf(id).begin(), ctrlUsersOf(id).end());
    // A predecessor of the node this chain closes over that belongs to another block cannot become
    // the successor's predecessor, that is not the successor itself and that is gone altogether
    // cannot either: an ordering joins two statements of one block, so the successor heads its own
    // block's chain instead (docs/design/effects/edges.cn.md 4.6).
    const auto survives = [&](gc_node_ref_t pred, gc_node_ref_t user) {
        return pred != user && pred != id && alive(pred) && nodeRegion(pred) == nodeRegion(user);
    };
    // While the control kind still exists it names the same pair beside the sync edge: the two
    // orderings have to agree until the control edges go (src/runtime/graph_edges.cpp,
    // `checkSyncEdges` is what reads the sync edge back). The edge is written rather than
    // inferred, because the path through the node this chain closes over is the one about to
    // disappear.
    const auto nameControlPair = [&](gc_node_ref_t user, gc_node_ref_t pred) {
        const auto ctrl = ctrlInputsOf(user);
        if (std::ranges::find(ctrl, pred) == ctrl.end()) {
            appendInput(DraftEdgeKind::Ctrl, user, pred);
        }
    };
    for (gc_node_ref_t user : syncUsers) {
        if (user == id || !alive(user)) {
            continue;
        }
// A sync edge joins two statements of one block, so a predecessor of the node this chain
        // closes over that belongs to another block cannot become the successor's predecessor: the
        // successor heads its own block's chain instead (docs/design/effects/edges.cn.md 4.6). The
        // code a splice moved into this block (the body of an inlined call) is a statement of it
        // (section 5.7), so the region a node was annotated with is looked through and the block a
        // statement is written in is what decides (statementRegionOf). The node the chain closes
        // over, and the user itself, are no predecessors of the user either.
        std::vector<gc_node_ref_t> kept;
        for (gc_node_ref_t pred : syncInputs) {
            if (pred != user && pred != id && alive(pred) && sameSyncBlock(*this, pred, user)) {
                kept.push_back(pred);
            }
        }
        setSyncInputs(user, kept);
        // The control edge names the same pair beside the sync edge: every consumer of the chain
        // that reads only that kind has to keep reading it, and the seal check reads the sync edge
        // back against the control one (src/runtime/graph_edges.cpp, `checkSyncEdges`). The edge is
        // written rather than inferred, because the path through the node this chain closes over is
        // the one about to disappear.
        for (gc_node_ref_t pred : kept) {
            nameControlPair(user, pred);
        }
    }
    // An ordering is not lost when the node that carried it goes: what the node this chain closes
    // over waited on is what the nodes that waited on it wait for -- the statement written before
    // it, and the shortcut beside the path the graph already had (`pred -> id -> user`). That is
    // the design's rule for deleting a node, which reconnects the order edges through it
    // (docs/design/effects/edges.cn.md 4: `A -> B -> C` keeps `A -> C` once `B` goes). It is
    // written in the control kind, which carries no `sync` statement mark of its own and is what
    // the paths that have not moved onto the sync edge yet write a wait in. A node that keeps a
    // wait of its own is left alone. A GATE is what this was first needed for: the commit refuses
    // one that waits for nothing (src/runtime/graph.cpp, the GATE check). What the node waited on
    // through neither kind -- a read of a `var` reads its writer through the data port alone -- is
    // not here to hand on: the fold that would remove such a read keeps it instead
    // (src/compile/gir/builder_expr.cpp, `collapseNonEscapingVariableReads`).
    const auto keepWaiting = [&](gc_node_ref_t user) {
        if (user == id || !alive(user)) {
            return;
        }
        if (header(user) == nullptr) {
            return;
        }
        for (gc_node_ref_t pred : ctrlInputsOf(user)) {
            if (pred != id) {
                return;
            }
        }
        for (gc_node_ref_t pred : syncInputsOf(user)) {
            if (pred != id) {
                return;
            }
        }
        for (gc_node_ref_t pred : syncInputs) {
            if (survives(pred, user)) {
                nameControlPair(user, pred);
            }
        }
        for (gc_node_ref_t pred : ctrlInputs) {
            if (survives(pred, user)) {
                nameControlPair(user, pred);
            }
        }
    };
    for (gc_node_ref_t user : ctrlUsers) {
        keepWaiting(user);
    }
    for (gc_node_ref_t user : syncUsers) {
        keepWaiting(user);
    }
}

void GraphDraft::handChainPlaceTo(gc_node_ref_t id, gc_node_ref_t tail) {
    requireOneWriting("handChainPlaceTo");
    if (id == tail || node(id) == nullptr || node(tail) == nullptr) {
        return;
    }
    ++mutations_;
    const std::vector<gc_node_ref_t> preds(syncInputsOf(id).begin(), syncInputsOf(id).end());
    const std::vector<gc_node_ref_t> users(syncUsersOf(id).begin(), syncUsersOf(id).end());
    const auto markStatement = [&](gc_node_ref_t n) {
        const DraftNodeHeader *h = header(n);
        if (h != nullptr) {
            setNodeRuntimeFlags(
                n, static_cast<uint8_t>(h->runtimeFlags | kGCNodeFlagSyncStatement));
        }
    };
    const auto chainTo = [&](gc_node_ref_t user, gc_node_ref_t pred) {
        markStatement(user);
        markStatement(pred);
        const gc_node_ref_t only[1] = {pred};
        setSyncInputs(user, only);
        const auto ctrl = ctrlInputsOf(user);
        if (std::ranges::find(ctrl, pred) == ctrl.end()) {
            appendInput(DraftEdgeKind::Ctrl, user, pred);
        }
    };
    markStatement(tail);
    // The statements written after `id` are written after the code that replaced it: they were
    // waiting on the place `id` had, and that place is `tail`'s now. The edge is written in both
    // kinds the ordering is carried as (src/runtime/graph_edges.cpp, `checkSyncEdges`).
    for (gc_node_ref_t user : users) {
        if (user != tail && alive(user)) {
            chainTo(user, tail);
        }
    }
    // The replacing code is entered from where `id` stood: with no chain of its own, `tail` heads
    // the chain of the block it joined. A tail that already stands in a chain keeps its place.
    if (syncInputsOf(tail).empty()) {
        for (gc_node_ref_t pred : preds) {
            if (pred != tail && alive(pred)) {
                chainTo(tail, pred);
                break;
            }
        }
    }
}

void GraphDraft::replaceOrderEdgesAt(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceOrderEdgesAt");
    if (oldId == newId || !alive(oldId) || !alive(newId)) {
        return;
    }
    ++mutations_;
    const uint32_t region = nodeRegion(newId);
    // What ordered the replaced node orders its replacer (docs/design/effects/edges.cn.md 4). An
    // ordering never leaves the region that holds it, so a predecessor of another region is not
    // moved; the replacer heads the ordering of its own region instead.
    const std::vector<gc_node_ref_t> preds(orderInputsOf(oldId).begin(), orderInputsOf(oldId).end());
    for (gc_node_ref_t pred : preds) {
        if (pred == newId || !alive(pred) || nodeRegion(pred) != region) {
            continue;
        }
        if (std::ranges::find(orderInputsOf(newId), pred) != orderInputsOf(newId).end()) {
            continue;
        }
        appendOrderDependency(newId, pred);
    }
    // What the replaced node ordered is ordered by its replacer. A user of another region cannot
    // take the new predecessor either: its own region's ordering is not this one, and the edge is
    // dropped with the node rather than moved (the seal check reads the regions back).
    const std::vector<gc_node_ref_t> users(orderUsersOf(oldId).begin(), orderUsersOf(oldId).end());
    for (gc_node_ref_t user : users) {
        if (user == newId || !alive(user)) {
            continue;
        }
        std::vector<gc_node_ref_t> inputs(orderInputsOf(user).begin(), orderInputsOf(user).end());
        const auto at = std::ranges::find(inputs, oldId);
        if (at == inputs.end()) {
            continue;
        }
        if (nodeRegion(user) != region) {
            inputs.erase(at);
        } else {
            *at = newId;
        }
        setOrderInputs(user, inputs);
    }
}

void GraphDraft::spliceOrderEdgesOver(gc_node_ref_t id) {
    requireOneWriting("spliceOrderEdgesOver");
    if (!alive(id)) {
        return;
    }
    const std::vector<gc_node_ref_t> preds(orderInputsOf(id).begin(), orderInputsOf(id).end());
    const std::vector<gc_node_ref_t> users(orderUsersOf(id).begin(), orderUsersOf(id).end());
    if (preds.empty() || users.empty()) {
        return;
    }
    ++mutations_;
    // Removing a node keeps the ordering that passed through it: a node ordered after it is
    // ordered after what it was ordered after (A -> B -> C becomes A -> C; docs/design/effects/
    // edges.cn.md 4). Both ends already carried an order edge, so both are nodes an order may sit
    // on; the copy is made because appendOrderDependency rewrites the runs it reads from.
    for (gc_node_ref_t user : users) {
        if (!alive(user)) {
            continue;
        }
        for (gc_node_ref_t pred : preds) {
            if (pred == id || pred == user || !alive(pred) ||
                nodeRegion(pred) != nodeRegion(user)) {
                continue;
            }
            if (std::ranges::find(orderInputsOf(user), pred) != orderInputsOf(user).end()) {
                continue;
            }
            appendOrderDependency(user, pred);
        }
    }
}

void GraphDraft::takeStatementPlace(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("takeStatementPlace");
    if (oldId == newId) {
        return;
    }
    const DraftNodeHeader *oldHeader = header(oldId);
    const DraftNodeHeader *newHeader = header(newId);
    if (oldHeader == nullptr || newHeader == nullptr) {
        return;
    }
    const bool sameRegion = nodeRegion(oldId) == nodeRegion(newId);
    if (sameRegion) {
        // The replaced node's order edges are part of what it hands over (docs/design/effects/
        // edges.cn.md 4), whether or not it also stood in a `sync` block's chain.
        replaceOrderEdgesAt(oldId, newId);
    }
    if ((oldHeader->runtimeFlags & kGCNodeFlagSyncStatement) == 0) {
        // Nothing stood in a chain, so there is no chain place to take.
        return;
    }
    if (!sameSyncBlock(*this, oldId, newId)) {
        // The replacer is not a statement of the block the replaced one was written in: the chain
        // closes over the replaced node instead (closeChainOver).
        return;
    }
    if (!syncInputsOf(newId).empty() || !syncUsersOf(newId).empty()) {
        // A node already written somewhere in a chain keeps that place: moving a second one onto it
        // would fork the chain.
        return;
    }
    ++mutations_;
    // The replacer is the block's statement now (docs/design/effects/edges.cn.md 1.5): the seal
    // check reads the flag back, and a sync edge may only join statements of the block
    // (docs/design/effects/edges.cn.md 4.6).
    setNodeRuntimeFlags(
        newId, static_cast<uint8_t>(newHeader->runtimeFlags | kGCNodeFlagSyncStatement));
    const std::vector<gc_node_ref_t> preds(syncInputsOf(oldId).begin(), syncInputsOf(oldId).end());
    const std::vector<gc_node_ref_t> users(syncUsersOf(oldId).begin(), syncUsersOf(oldId).end());
    std::vector<gc_node_ref_t> kept;
    for (gc_node_ref_t pred : preds) {
        if (alive(pred) && nodeRegion(pred) == nodeRegion(newId)) {
            kept.push_back(pred);
        }
    }
    setSyncInputs(newId, kept);
    for (gc_node_ref_t pred : kept) {
        const auto ctrl = ctrlInputsOf(newId);
        if (std::ranges::find(ctrl, pred) == ctrl.end()) {
            appendInput(DraftEdgeKind::Ctrl, newId, pred);
        }
    }
    for (gc_node_ref_t user : users) {
        if (user == newId || !alive(user) || !sameSyncBlock(*this, user, newId)) {
            continue;
        }
        const gc_node_ref_t only[1] = {newId};
        setSyncInputs(user, only);
        const auto ctrl = ctrlInputsOf(user);
        if (std::ranges::find(ctrl, newId) == ctrl.end()) {
            appendInput(DraftEdgeKind::Ctrl, user, newId);
        }
    }
}

void GraphDraft::eraseNode(gc_node_ref_t id) {
    requireOneWriting("eraseNode");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot erase a missing draft node.");
    if (draftNode->header.kind == GCNodeKind::Brch || draftNode->header.kind == GCNodeKind::Loop) {
        dissolveRegionsOf(id);
    }

    const auto copy = [](std::span<const gc_node_ref_t> refs) {
        return std::vector<gc_node_ref_t>(refs.begin(), refs.end());
    };
    const std::vector<gc_node_ref_t> normInputs = copy(DraftNodeView::normInputs(draftNode));
    const std::vector<gc_node_ref_t> withInputs = copy(DraftNodeView::withInputs(draftNode));
    const std::vector<gc_node_ref_t> ctrlInputs = copy(DraftNodeView::ctrlInputs(draftNode));
    const std::vector<gc_node_ref_t> orderInputs = copy(DraftNodeView::orderInputs(draftNode));
    const std::vector<gc_node_ref_t> syncInputs = copy(DraftNodeView::syncInputs(draftNode));
    const std::vector<gc_node_ref_t> normUsers  = copy(usersOf(id, DraftEdgeSlot::NormalPorts));
    const std::vector<gc_node_ref_t> withUsers  = copy(usersOf(id, DraftEdgeSlot::ContextPorts));
    const std::vector<gc_node_ref_t> ctrlUsers  = copy(usersOf(id, DraftEdgeSlot::Ctrl));
    const std::vector<gc_node_ref_t> orderUsers = copy(usersOf(id, DraftEdgeSlot::Order));
    const std::vector<gc_node_ref_t> syncUsers  = copy(usersOf(id, DraftEdgeSlot::Sync));
    // The block's chain closes over a statement the graph loses: its successors follow what it
    // followed, and the control edge naming the same pair is written beside the sync edge.
    closeChainOver(id);
    // The orderings that passed through the node survive it the same way (docs/design/effects/
    // edges.cn.md 4), and both splices run before the node's own runs go.
    spliceOrderEdgesOver(id);

    for (gc_node_ref_t input : normInputs) {
        removeUserRef(input, id, DraftEdgeSlot::NormalPorts);
    }
    for (gc_node_ref_t input : withInputs) {
        removeUserRef(input, id, DraftEdgeSlot::ContextPorts);
    }
    for (gc_node_ref_t input : ctrlInputs) {
        removeUserRef(input, id, DraftEdgeSlot::Ctrl);
    }
    for (gc_node_ref_t input : orderInputs) {
        removeUserRef(input, id, DraftEdgeSlot::Order);
    }
    for (gc_node_ref_t input : syncInputs) {
        removeUserRef(input, id, DraftEdgeSlot::Sync);
    }
    // Whoever reads the node stops reading it, as often as it did. The lists of users are
    // kept in step with the inputs, so nothing else needs looking at.
    const auto unlinkAll = [&](DraftEdgeKind kind, const std::vector<gc_node_ref_t> &users,
                               PortRole role = PortRole::Normal) {
        for (gc_node_ref_t userId : users) {
            if (userId != id && alive(userId)) {
                while (unlinkInput(kind, userId, id, role)) {
                }
            }
        }
    };
    unlinkAll(DraftEdgeKind::Data, normUsers);
    unlinkAll(DraftEdgeKind::Data, withUsers, PortRole::Context);
    unlinkAll(DraftEdgeKind::Ctrl, ctrlUsers);
    unlinkAll(DraftEdgeKind::Order, orderUsers);
    unlinkAll(DraftEdgeKind::Sync, syncUsers);
    if (id < users_.size()) {
        users_[id].reset();
    }

    touchRefs();
    nodesById_[id] = nullptr;
    liveNodeCount_--;
    std::erase(normPorts_, id);
    std::erase(withPorts_, id);
    std::erase(closureNodes_, id);
    if (entry_ == id)
        entry_ = kInvalidNodeRef;
    if (exit_ == id)
        exit_ = kInvalidNodeRef;
    if (output_ == id)
        output_ = kInvalidNodeRef;
    if (returnNode_ == id) {
        returnNode_ = kInvalidNodeRef;
        returnKind_ = GCReturnKind::None;
    }
}

void GraphDraft::setNormInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    requireOneWriting("setNormInputs");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set norm inputs for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::normInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::NormalPorts);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.normInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, DraftEdgeSlot::NormalPorts);
    }
}

void GraphDraft::setWithInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    requireOneWriting("setWithInputs");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set with inputs for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::withInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::ContextPorts);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.withInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, DraftEdgeSlot::ContextPorts);
    }
}

void GraphDraft::setCtrlInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    requireOneWriting("setCtrlInputs");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set ctrl inputs for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::ctrlInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::Ctrl);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.ctrlInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, DraftEdgeSlot::Ctrl);
    }
}

void GraphDraft::setOrderInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    requireOneWriting("setOrderInputs");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set order inputs for a missing draft node.");
    for (gc_node_ref_t input : inputs) {
        if (input == id) {
            throw std::logic_error("An order edge cannot order a node after itself.");
        }
    }
    for (gc_node_ref_t oldInput : DraftNodeView::orderInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::Order);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.orderInputs   = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, DraftEdgeSlot::Order);
    }
}

void GraphDraft::setSyncInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    requireOneWriting("setSyncInputs");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set sync inputs for a missing draft node.");
    for (gc_node_ref_t input : inputs) {
        if (input == id) {
            throw std::logic_error("A sync edge cannot chain a node to itself.");
        }
    }
    // The chain is a chain: one statement is written directly before another, so a node has one
    // sync predecessor at most (the tail of the chain reaching the block's value is the one edge
    // that leaves it, and it is the same shape seen from the other end).
    if (inputs.size() > 1) {
        throw std::logic_error("A sync edge joins one statement to the next one.");
    }
    for (gc_node_ref_t oldInput : DraftNodeView::syncInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::Sync);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.syncInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, DraftEdgeSlot::Sync);
    }
}

bool GraphDraft::setSyncPredecessor(gc_node_ref_t id, gc_node_ref_t predecessor) {
    if (predecessor == id || node(id) == nullptr || node(predecessor) == nullptr) {
        return false;
    }
    if (!syncInputsOf(id).empty() || !syncUsersOf(predecessor).empty()) {
        // Both are the statement's own place in the chain: a node already in one chain cannot be
        // put into another, and a chain never forks.
        return false;
    }
    const gc_node_ref_t only[1] = {predecessor};
    setSyncInputs(id, only);
    return true;
}

bool GraphDraft::appendOrderDependency(gc_node_ref_t id, gc_node_ref_t predecessor) {
    if (predecessor == id || node(id) == nullptr || node(predecessor) == nullptr) {
        return false;
    }
    if (std::ranges::find(orderInputsOf(id), predecessor) != orderInputsOf(id).end()) {
        return false;
    }
    std::vector<gc_node_ref_t> inputs(orderInputsOf(id).begin(), orderInputsOf(id).end());
    inputs.push_back(predecessor);
    setOrderInputs(id, inputs);
    return true;
}

void GraphDraft::setPorts(
    gc_node_ref_t id, std::span<const gc_node_ref_t> ports,
    std::span<const gc_node_ref_t> withPorts) {
    requireOneWriting("setPorts");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set ports for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::normInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::NormalPorts);
    }
    for (gc_node_ref_t oldInput : DraftNodeView::withInputs(draftNode)) {
        removeUserRef(oldInput, id, DraftEdgeSlot::ContextPorts);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.normInputs    = ports;
    init.withInputs    = withPorts;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : ports) {
        appendUserRef(input, id, DraftEdgeSlot::NormalPorts);
    }
    for (gc_node_ref_t input : withPorts) {
        appendUserRef(input, id, DraftEdgeSlot::ContextPorts);
    }
}

void GraphDraft::replaceUsesInList(
    gc_node_ref_t ownerId, std::span<const gc_node_ref_t> users, gc_node_ref_t oldId,
    gc_node_ref_t newId, DraftEdgeSlot slot) {
    ++mutations_;
    for (gc_node_ref_t userId : users) {
        DraftNode *userNode = node(userId);
        if (userNode == nullptr) {
            continue;
        }
        DraftNodeInit init = makeInitFromNode(userNode);
        std::vector<gc_node_ref_t> replaced(initSlot(init, slot).begin(), initSlot(init, slot).end());
        for (gc_node_ref_t &item : replaced) {
            if (item == oldId) {
                item = newId;
            }
        }
        initSlot(init, slot) = replaced;
        replaceNodeStorage(userId, rebuildNode(userId, init));
        removeUserRef(ownerId, userId, slot);
        appendUserRef(newId, userId, slot);
    }
}

void GraphDraft::replaceValueStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    auto replaceOne = [&](gc_node_ref_t &ref) {
        if (ref == oldId) {
            ref = newId;
        }
    };

    replaceOne(output_);
    replaceOne(returnNode_);
}

void GraphDraft::replaceCtrlStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    auto replaceOne = [&](gc_node_ref_t &ref) {
        if (ref == oldId) {
            ref = newId;
        }
    };

    replaceOne(entry_);
    replaceOne(exit_);

    for (gc_node_ref_t id : brchIds_) {
        DraftNode *draftNode = node(id);
        if (!draftNode || draftNode->header.kind != GCNodeKind::Brch) {
            continue;
        }
        auto *payload = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
        replaceOne(payload->join);
    }
    for (gc_node_ref_t id : joinIds_) {
        DraftNode *draftNode = node(id);
        if (!draftNode || draftNode->header.kind != GCNodeKind::Join ||
            draftNode->header.payloadBytes < sizeof(GCJoinBody)) {
            continue;
        }
        replaceOne(DraftNodeView::payloadAs<GCJoinBody>(draftNode)->brch);
    }
}

void GraphDraft::replaceSyncStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    // A sync edge into the block's value is a statement chain that ends at the value: the value
    // node is not a separate anchor of the graph, so nothing but the run itself refers to it.
    (void)oldId;
    (void)newId;
}

void GraphDraft::replaceAllNormUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceAllNormUses");
    ++mutations_;
    std::vector<gc_node_ref_t> users(normUsersOf(oldId).begin(), normUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, DraftEdgeSlot::NormalPorts);
    replaceValueStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllWithUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceAllWithUses");
    ++mutations_;
    std::vector<gc_node_ref_t> users(withUsersOf(oldId).begin(), withUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, DraftEdgeSlot::ContextPorts);
    replaceValueStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllCtrlUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceAllCtrlUses");
    ++mutations_;
    std::vector<gc_node_ref_t> users(ctrlUsersOf(oldId).begin(), ctrlUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, DraftEdgeSlot::Ctrl);
    replaceCtrlStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllOrderUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceAllOrderUses");
    ++mutations_;
    std::vector<gc_node_ref_t> users(orderUsersOf(oldId).begin(), orderUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, DraftEdgeSlot::Order);
}

void GraphDraft::replaceAllSyncUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceAllSyncUses");
    ++mutations_;
    std::vector<gc_node_ref_t> users(syncUsersOf(oldId).begin(), syncUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, DraftEdgeSlot::Sync);
    replaceSyncStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllValueUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    requireOneWriting("replaceAllValueUses");
    ++mutations_;
    replaceAllNormUses(oldId, newId);
    replaceAllWithUses(oldId, newId);
}

void GraphDraft::dropUncalledGraphs(std::span<const GraphId> graphs) {
    ++mutations_;
    std::vector<GraphId> called;
    for (gc_node_ref_t id = 0; id < nodesById_.size(); ++id) {
        const DraftNodeHeader *h = header(id);
        if (h && h->kind == GCNodeKind::Func) {
            called.push_back(calleeIdOf(id));
        } else if (h && h->kind == GCNodeKind::Loop) {
            const GCLoopBody loop = loopGraphsOf(id);
            called.push_back(loop.condGraph);
            called.push_back(loop.bodyGraph);
        }
    }
    const auto gone = [&](GraphId graph) {
        return std::ranges::find(graphs, graph) != graphs.end() &&
               std::ranges::find(called, graph) == called.end();
    };
    std::erase_if(dependencies_, gone);
    std::erase_if(subGraphs_, gone);
    touchRefs();
}

size_t GraphDraft::dissolveUnorderedGates() {
    size_t removed = 0;
    for (bool changed = true; changed;) {
        changed = false;
        // The ascending ids of the gates: the order a scan of every node would take.
        std::erase_if(gateIds_, [&](gc_node_ref_t id) {
            const DraftNodeHeader *h = header(id);
            return !h || h->kind != GCNodeKind::Gate;
        });
        std::ranges::sort(gateIds_);
        gateIds_.erase(std::ranges::unique(gateIds_).begin(), gateIds_.end());
        const std::vector<gc_node_ref_t> gates = gateIds_;
        for (gc_node_ref_t id : gates) {
            const DraftNodeHeader *h = header(id);
            // A GATE waits on the sync chain's tail (docs/design/effects/edges.cn.md 5.3): its wait
            // is a **sync predecessor**, and 5.3 keeps a GATE with one -- it "may not be deleted,
            // and may not be replaced by its input, unless its sync predecessor is already gone".
            // A gate whose wait is a control input is what that wait used to be written as; the
            // ones this lane has not moved onto the sync edge keep the old condition. A gate with
            // neither waits on nothing, and is plain forwarding.
            if (!h || h->kind != GCNodeKind::Gate || !syncInputsOf(id).empty() ||
                !ctrlInputsOf(id).empty() || normInputsOf(id).empty()) {
                continue;
            }
            const gc_node_ref_t value = normInputsOf(id).back();
            if (exit_ == id) {
                exit_ = value;
            }
            if (output_ == id) {
                output_ = value;
            }
            if (returnNode_ == id) {
                returnNode_ = value;
            }
            replaceAllValueUses(id, value);
            eraseNode(id); // its control users lose nothing: it waited on nothing
            ++removed;
            changed = true;
        }
    }
    return removed;
}

gc_slot_idx_t GraphDraft::allocateRuntimeSlot(camel::core::type::Type *type) {
    ++mutations_;
    ASSERT(type != nullptr, "Draft runtime slot allocation requires a non-null type.");
    if (runtimeSlotTypes_.empty()) {
        runtimeSlotTypes_.push_back(camel::core::type::Type::Void()); // slot 0 is reserved
    }
    hasFrameLayout_        = true;
    const size_t slotIndex = runtimeSlotTypes_.size();
    ASSERT(
        slotIndex < static_cast<size_t>(std::numeric_limits<gc_slot_idx_t>::max()),
        "Draft runtime slot count exceeds the data-index range.");
    // A value of `never` never exists (design principles 3.9): the expression that has this type
    // gives no value at all, so its slot holds nothing, like the slot of a `void` value. The
    // graph keeps the name (the type of an `exit` call, the result type of a function that never
    // returns), and no runtime layout is ever made of it.
    runtimeSlotTypes_.push_back(
        type->code() == camel::core::type::TypeCode::Never ? camel::core::type::Type::Void()
                                                           : type);
    return static_cast<gc_slot_idx_t>(slotIndex);
}

size_t GraphDraft::appendStaticSlot(slot_t value, camel::core::type::Type *type) {
    ++mutations_;
    if (staticSlots_.empty()) {
        staticSlots_.push_back(NullSlot);
        staticSlotTypes_.push_back(camel::core::type::Type::Void());
    }
    touchRefs();
    staticSlots_.push_back(value);
    staticSlotTypes_.push_back(type);
    return staticSlots_.size() - 1;
}

gc_node_ref_t GraphDraft::materializeStaticValue(
    slot_t value, camel::core::type::Type *type, uint8_t runtimeFlags) {
    ++mutations_;
    const size_t slotIndex = appendStaticSlot(value, type);
    ASSERT(
        slotIndex <= static_cast<size_t>(std::numeric_limits<gc_slot_idx_t>::max()),
        "Draft static slot index exceeds the data-index range.");
    DraftNodeInit init{
        .dataIndex    = static_cast<gc_slot_idx_t>(-static_cast<int32_t>(slotIndex)),
        .dataType     = type,
        .kind         = GCNodeKind::Data,
        .runtimeFlags = static_cast<uint8_t>(runtimeFlags | kGCNodeFlagConstant),
    };
    return addNode(init);
}

void GraphDraft::setStaticSlot(size_t index, slot_t value) {
    requireOneWriting("setStaticSlot");
    ++mutations_;
    ASSERT(index < staticSlots_.size(), "Draft static slot index is out of range.");
    touchRefs();
    staticSlots_[index] = value;
}

void GraphDraft::relocateStaticValues(
    const camel::core::rtdata::Object::RefRelocator &relocate) {
    for (size_t i = 0; i < staticSlots_.size(); ++i) {
        const camel::core::type::Type *type =
            i < staticSlotTypes_.size() ? staticSlotTypes_[i] : nullptr;
        if (!type || !type->isGCTraced() || staticSlots_[i] == NullSlot) {
            continue;
        }
        auto *ref = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(staticSlots_[i]);
        if (!ref) {
            continue;
        }
        ref = relocate(
            ref,
            type,
            camel::core::rtdata::RefTraceInfo{
                .owner     = nullptr,
                .ownerType = nullptr,
                .slotType  = type,
                .ownerKind = "GraphDraft",
                .slotName  = {},
                .slotIndex = i,
            });
        relocateStaticSlot(i, camel::core::rtdata::toSlot(ref));
    }
}

void GraphDraft::addDependency(GraphId graph) {
    ++mutations_;
    touchRefs();
    if (isValid(graph) &&
        std::find(dependencies_.begin(), dependencies_.end(), graph) == dependencies_.end()) {
        dependencies_.push_back(graph);
    }
}

void GraphDraft::addSubGraph(GraphId graph) {
    ++mutations_;
    touchRefs();
    if (isValid(graph) &&
        std::find(subGraphs_.begin(), subGraphs_.end(), graph) == subGraphs_.end()) {
        subGraphs_.push_back(graph);
    }
}

void GraphDraft::addCaptureName(std::string_view name) {
    ++mutations_;
    captureNames_.push_back(camel::core::internSymbol(name));
}

void GraphDraft::addStaticGraphRef(GraphId graph) {
    ++mutations_;
    touchRefs();
    if (isValid(graph) && std::find(staticGraphRefs_.begin(), staticGraphRefs_.end(), graph) ==
                              staticGraphRefs_.end()) {
        staticGraphRefs_.push_back(graph);
    }
}

void GraphDraft::addRuleGraphRef(GraphId graph) {
    ++mutations_;
    touchRefs();
    if (isValid(graph) &&
        std::find(ruleGraphs_.begin(), ruleGraphs_.end(), graph) == ruleGraphs_.end()) {
        ruleGraphs_.push_back(graph);
    }
}

uint32_t GraphDraft::payloadGraphIndexOf(GraphId graph) {
    ++mutations_;
    if (!isValid(graph)) {
        return kNoLoopGraph;
    }
    const auto [it, added] =
        payloadGraphIndex_.emplace(graph, static_cast<uint32_t>(payloadGraphs_.size()));
    if (added) {
        payloadGraphs_.push_back(graph);
    }
    return it->second;
}

GraphId GraphDraft::payloadGraphAt(uint32_t index) const {
    if (index == kNoLoopGraph) {
        return kInvalidGraphId;
    }
    if (index >= payloadGraphs_.size()) {
        throw std::runtime_error(
            std::format(
                "A payload names graph {} of {} in the reference table.",
                index,
                payloadGraphs_.size()));
    }
    return payloadGraphs_[index];
}

GraphId GraphDraft::calleeIdOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr && draftNode->header.kind == GCNodeKind::Func, "Not a FUNC node.");
    return payloadGraphAt(DraftNodeView::payloadAs<GCFuncRef>(draftNode)->ref);
}

GCGraph *GraphDraft::calleeGraphOf(gc_node_ref_t id) const {
    const GraphId callee = calleeIdOf(id);
    return program_ != nullptr && isValid(callee) ? program_->graph(callee) : nullptr;
}

void GraphDraft::setCalleeGraph(gc_node_ref_t id, GraphId callee) {
    requireOneWriting("setCalleeGraph");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr && draftNode->header.kind == GCNodeKind::Func, "Not a FUNC node.");
    touchRefs();
    DraftNodeView::payloadAs<GCFuncRef>(draftNode)->ref = payloadGraphIndexOf(callee);
}

GCLoopBody GraphDraft::loopGraphsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr && draftNode->header.kind == GCNodeKind::Loop, "Not a LOOP node.");
    const auto *ref = DraftNodeView::payloadAs<GCLoopRef>(draftNode);
    return GCLoopBody{
        .condGraph       = payloadGraphAt(ref->condRef),
        .bodyGraph       = payloadGraphAt(ref->bodyRef),
        .kind            = ref->kind,
        .iterableForm    = ref->iterableForm,
        .collectionCount = ref->collectionCount,
    };
}

void GraphDraft::setLoopGraphs(gc_node_ref_t id, GraphId cond, GraphId body) {
    requireOneWriting("setLoopGraphs");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr && draftNode->header.kind == GCNodeKind::Loop, "Not a LOOP node.");
    touchRefs();
    auto *ref    = DraftNodeView::payloadAs<GCLoopRef>(draftNode);
    ref->condRef = payloadGraphIndexOf(cond);
    ref->bodyRef = payloadGraphIndexOf(body);
}

GCGraph *GraphDraft::encode(const std::string &mangledName, const std::string &name) const {
    auto *debugRecord = createGraphDebugRecord(mangledName, name);
    recordDraftDebugData(debugRecord, *this);
    camel::core::type::TupleType *staticDataType = nullptr;
    if (!staticSlotTypes_.empty()) {
        staticDataType = camel::core::type::TupleType::layout(staticSlotTypes_);
    }
    const auto payloadShape = describeDraftNativePayload(*this);

    ASSERT(isValid(id_), "Encoding a graph draft requires its GraphId.");
    return GCGraphBuildAccess::create(
        id_,
        GCGraphDerivation{typeSpecializationRoot_, flags_},
        debugRecord,
        funcType_,
        frameSlotTypes(),
        staticDataType,
        closureType_,
        outerGraphId_,
        dependencies_,
        subGraphs_,
        staticGraphRefs_,
        ruleGraphs_,
        payloadShape,
        [&](GCGraphPayloadArena &payload) { emitDraftNativePayload(*this, payload); },
        staticSlots_);
}

} // namespace camel::runtime

