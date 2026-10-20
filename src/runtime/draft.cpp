/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 10, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

size_t adjacencyBytes(gc_cnt_t normInputs, gc_cnt_t withInputs, gc_cnt_t ctrlInputs) {
    return (static_cast<size_t>(normInputs) + static_cast<size_t>(withInputs) +
            static_cast<size_t>(ctrlInputs)) *
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
    init.payload      = DraftNodeView::payload(node);
    init.normInputs   = DraftNodeView::normInputs(node);
    init.withInputs   = DraftNodeView::withInputs(node);
    init.ctrlInputs   = DraftNodeView::ctrlInputs(node);
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
    draftNode->header.ctrlInputCount = static_cast<gc_cnt_t>(init.ctrlInputs.size());
    draftNode->header.kind           = init.kind;
    draftNode->header.runtimeFlags   = init.runtimeFlags;
    draftNode->header.origin         = init.origin;
    draftNode->header.permissions    = init.permissions;
    draftNode->header.attrs          = init.attrs;
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
    auto bytes = tailBytes(node) + node->header.payloadBytes;
    return edgeSpan(bytes, node->header.normInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::normInputs(const DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes;
    return edgeSpan(bytes, node->header.normInputCount);
}

std::span<gc_node_ref_t> DraftNodeView::withInputs(DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 static_cast<size_t>(node->header.normInputCount) * sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.withInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::withInputs(const DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 static_cast<size_t>(node->header.normInputCount) * sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.withInputCount);
}

std::span<gc_node_ref_t> DraftNodeView::ctrlInputs(DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.ctrlInputCount);
}

std::span<const gc_node_ref_t> DraftNodeView::ctrlInputs(const DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.ctrlInputCount);
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

std::span<const GCBranchArm> GraphDraft::branchArmsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    if (draftNode == nullptr || draftNode->header.kind != GCNodeKind::Brch) {
        return {};
    }
    const auto *payload = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
    const auto *arms    = reinterpret_cast<const GCBranchArm *>(
        DraftNodeView::payload(draftNode).data() + sizeof(DraftBrchPayload));
    return {arms, static_cast<size_t>(payload->armCount)};
}

std::span<const GCBranchArm> GraphDraft::branchArmsOf(const DraftNode *nodePtr) const {
    return branchArmsOf(nodeId(nodePtr));
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

std::span<const gc_node_ref_t> GraphDraft::ctrlInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::ctrlInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlInputsOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? DraftNodeView::ctrlInputs(nodePtr)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::usersOf(gc_node_ref_t id, DraftEdgeKind kind) const {
    if (id >= users_.size() || !users_[id]) {
        return {};
    }
    return users_[id]->of(kind);
}

std::vector<gc_node_ref_t> &GraphDraft::userListOf(gc_node_ref_t id, DraftEdgeKind kind) {
    ++mutations_;
    if (users_.size() <= id) {
        users_.resize(nodesById_.size());
    }
    if (!users_[id]) {
        users_[id] = std::make_unique<DraftUserLists>();
    }
    return users_[id]->of(kind);
}

std::span<const gc_node_ref_t> GraphDraft::normUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeKind::Norm);
}

std::span<const gc_node_ref_t> GraphDraft::normUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeKind::Norm)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::withUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeKind::With);
}

std::span<const gc_node_ref_t> GraphDraft::withUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeKind::With)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlUsersOf(gc_node_ref_t id) const {
    return usersOf(id, DraftEdgeKind::Ctrl);
}

std::span<const gc_node_ref_t> GraphDraft::ctrlUsersOf(const DraftNode *nodePtr) const {
    return containsNode(nodePtr) ? usersOf(nodePtr->header.selfId, DraftEdgeKind::Ctrl)
                                 : std::span<const gc_node_ref_t>{};
}

std::span<gc_node_ref_t> GraphDraft::mutableInputs(DraftNode *node, DraftEdgeKind kind) {
    ++mutations_;
    return kind == DraftEdgeKind::Norm   ? DraftNodeView::normInputs(node)
           : kind == DraftEdgeKind::With ? DraftNodeView::withInputs(node)
                                         : DraftNodeView::ctrlInputs(node);
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

bool GraphDraft::isBranchArmAnchor(gc_node_ref_t id) const {
    for (gc_node_ref_t draftId : brchIds_) {
        const DraftNodeHeader *draftHeader = header(draftId);
        if (!draftHeader || draftHeader->kind != GCNodeKind::Brch) {
            continue;
        }
        for (const GCBranchArm &arm : branchArmsOf(draftId)) {
            if (arm.head == id || arm.tail == id) {
                return true;
            }
        }
    }
    return false;
}

bool GraphDraft::isBranchArmAnchor(const DraftNode *nodePtr) const {
    return isBranchArmAnchor(nodeId(nodePtr));
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
        return sizeof(DraftBrchPayload) + graph->branchArmsOf(sourceRef).size_bytes();
    }
    return graph->nodePayloadBytes(sourceRef).size();
}

DraftNode *GraphDraft::createDecodedNode(
    const GCGraph *graph, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &sourceToDraft) {
    ++mutations_;
    const GCNode *sourceNode = graph->node(sourceRef);
    ASSERT(sourceNode != nullptr, "Draft decode encountered a null source node.");

    const auto normInputs = graph->normInputsOf(sourceRef);
    const auto withInputs = graph->withInputsOf(sourceRef);
    const auto ctrlInputs = graph->ctrlInputsOf(sourceRef);

    const uint32_t payloadBytes =
        static_cast<uint32_t>(payloadBytesForSourceNode(graph, sourceRef));
    const size_t tailBytes =
        static_cast<size_t>(payloadBytes) + adjacencyBytes(
                                                static_cast<gc_cnt_t>(normInputs.size()),
                                                static_cast<gc_cnt_t>(withInputs.size()),
                                                static_cast<gc_cnt_t>(ctrlInputs.size()));

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
    draftNode->header.kind             = sourceNode->kind;
    draftNode->header.runtimeFlags     = sourceNode->flags;
    draftNode->header.origin           = graph->nodeOrigin(sourceRef);
    draftNode->header.permissions      = graph->nodePermissions(sourceRef);
    draftNode->header.attrs            = graph->nodeAttrs(sourceRef);
    draftNode->header.storageCls       = storageClass;

    if (sourceNode->kind == GCNodeKind::Brch) {
        const auto sourceArms         = graph->branchArmsOf(sourceRef);
        auto *payload                 = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
        const auto *sourceBrchPayload = graph->nodeBodyAs<GCBrchBody>(sourceRef);
        payload->join                 = translateNodeRef(sourceBrchPayload->join, sourceToDraft);
        payload->armCount             = sourceBrchPayload->armCount;
        payload->defaultArm = translateNodeRef(sourceBrchPayload->defaultArm, sourceToDraft);
        auto *armBytes      = reinterpret_cast<GCBranchArm *>(
            DraftNodeView::payload(draftNode).data() + sizeof(DraftBrchPayload));
        for (size_t i = 0; i < sourceArms.size(); ++i) {
            armBytes[i].head = translateNodeRef(sourceArms[i].head, sourceToDraft);
            armBytes[i].tail = translateNodeRef(sourceArms[i].tail, sourceToDraft);
        }
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
    draft->draftIdsBySourceRef_.assign(graph->nodeCount(), kInvalidNodeRef);

    std::vector<gc_node_ref_t> sourceToDraft(graph->nodeCount(), kInvalidNodeRef);
    draft->nodesById_.reserve(graph->nodeCount());
    draft->sourceRefsById_.reserve(graph->nodeCount());
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const gc_node_ref_t sourceRef = it.ref();
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
        const auto decodeUsers = [&](DraftEdgeKind kind, std::span<const gc_node_ref_t> users) {
            if (users.empty()) {
                return;
            }
            auto &list = draft->userListOf(draftId, kind);
            list.reserve(users.size());
            for (gc_node_ref_t user : users) {
                list.push_back(translateNodeRef(user, sourceToDraft));
            }
        };
        decodeUsers(DraftEdgeKind::Norm, graph->normOutputsOf(sourceRef));
        decodeUsers(DraftEdgeKind::With, graph->withOutputsOf(sourceRef));
        decodeUsers(DraftEdgeKind::Ctrl, graph->ctrlOutputsOf(sourceRef));
        draft->noteNodeKind(draftId, draft->nodesById_[draftId]->header.kind);
        draft->liveNodeCount_++;
    }

    auto translateVector = [&](std::span<const gc_node_ref_t> sourceRefs,
                               std::vector<gc_node_ref_t> &target) {
        target.clear();
        target.reserve(sourceRefs.size());
        for (gc_node_ref_t ref : sourceRefs) {
            target.push_back(translateNodeRef(ref, sourceToDraft));
        }
    };

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
                                        static_cast<gc_cnt_t>(init.ctrlInputs.size()));

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
        DraftNodeInit copiedInit{
            .dataIndex    = init.dataIndex,
            .dataType     = init.dataType,
            .kind         = init.kind,
            .runtimeFlags = init.runtimeFlags,
            .origin       = init.origin,
            .permissions  = init.permissions,
            .attrs        = init.attrs,
            .payload      = payloadCopy,
            .normInputs   = normInputsCopy,
            .withInputs   = withInputsCopy,
            .ctrlInputs   = ctrlInputsCopy,
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

void GraphDraft::removeUserRef(
    gc_node_ref_t id, gc_node_ref_t userId, bool norm, bool with, bool ctrl) {
    ++mutations_;
    if (node(id) == nullptr || id >= users_.size() || !users_[id]) {
        return;
    }
    const auto eraseOne = [userId](std::vector<gc_node_ref_t> &items) {
        // The user that was linked last is the one most often unlinked first.
        auto it = std::find(items.rbegin(), items.rend(), userId);
        if (it != items.rend()) {
            items.erase(std::next(it).base());
        }
    };
    if (norm)
        eraseOne(users_[id]->norm);
    if (with)
        eraseOne(users_[id]->with);
    if (ctrl)
        eraseOne(users_[id]->ctrl);
}

void GraphDraft::appendUserRef(
    gc_node_ref_t id, gc_node_ref_t userId, bool norm, bool with, bool ctrl) {
    ++mutations_;
    if (node(id) == nullptr) {
        return;
    }
    // Called once the user reads `id` in the group: the user is in the list already when the
    // group holds `id` more than once, or the list says so.
    const auto appendUnique = [&](DraftEdgeKind kind) {
        auto &items = userListOf(id, kind);
        if (!items.empty() && items.back() == userId) {
            return;
        }
        if (items.size() > 32) {
            const auto inputs = kind == DraftEdgeKind::Norm   ? normInputsOf(userId)
                                : kind == DraftEdgeKind::With ? withInputsOf(userId)
                                                              : ctrlInputsOf(userId);
            if (inputs.size() <= 64) {
                if (std::count(inputs.begin(), inputs.end(), id) > 1) {
                    return;
                }
                items.push_back(userId);
                return;
            }
        }
        if (std::find(items.begin(), items.end(), userId) == items.end()) {
            items.push_back(userId);
        }
    };
    if (norm)
        appendUnique(DraftEdgeKind::Norm);
    if (with)
        appendUnique(DraftEdgeKind::With);
    if (ctrl)
        appendUnique(DraftEdgeKind::Ctrl);
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

uint8_t GraphDraft::nodePermissions(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    return h ? h->permissions : 0;
}

void GraphDraft::setNodePermissions(gc_node_ref_t id, uint8_t permissions) {
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
    sourceRefsById_.push_back(kInvalidNodeRef);
    noteNodeKind(id, init.kind);
    liveNodeCount_++;

    for (gc_node_ref_t input : init.normInputs) {
        appendUserRef(input, id, true, false, false);
    }
    for (gc_node_ref_t input : init.withInputs) {
        appendUserRef(input, id, false, true, false);
    }
    for (gc_node_ref_t input : init.ctrlInputs) {
        appendUserRef(input, id, false, false, true);
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

void GraphDraft::appendInputEntry(gc_node_ref_t userId, DraftEdgeKind kind, gc_node_ref_t inputId) {
    ++mutations_;
    DraftNode *draftNode = node(userId);
    DraftNodeHeader &h   = draftNode->header;
    if (h.usedBytes() + sizeof(gc_node_ref_t) > h.capacityBytes) {
        // No room: lay the node out again with the new input (the block grows by doubling).
        DraftNodeInit init = makeInitFromNode(draftNode);
        std::vector<gc_node_ref_t> grown;
        const auto current = kind == DraftEdgeKind::Norm   ? init.normInputs
                             : kind == DraftEdgeKind::With ? init.withInputs
                                                           : init.ctrlInputs;
        grown.assign(current.begin(), current.end());
        grown.push_back(inputId);
        (kind == DraftEdgeKind::Norm   ? init.normInputs
         : kind == DraftEdgeKind::With ? init.withInputs
                                       : init.ctrlInputs) = grown;
        replaceNodeStorage(userId, rebuildNode(userId, init));
        return;
    }
    // Room in the block: the entry goes at the end of its group, the groups after it move up.
    auto *base = reinterpret_cast<gc_node_ref_t *>(draftNode->tail + h.payloadBytes);
    const size_t normN = h.normInputCount, withN = h.withInputCount, ctrlN = h.ctrlInputCount;
    const size_t at = kind == DraftEdgeKind::Norm   ? normN
                      : kind == DraftEdgeKind::With ? normN + withN
                                                    : normN + withN + ctrlN;
    std::memmove(base + at + 1, base + at, (normN + withN + ctrlN - at) * sizeof(gc_node_ref_t));
    base[at] = inputId;
    (kind == DraftEdgeKind::Norm   ? h.normInputCount
     : kind == DraftEdgeKind::With ? h.withInputCount
                                   : h.ctrlInputCount)++;
}

void GraphDraft::eraseNode(gc_node_ref_t id) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot erase a missing draft node.");

    const auto copy = [](std::span<const gc_node_ref_t> refs) {
        return std::vector<gc_node_ref_t>(refs.begin(), refs.end());
    };
    const std::vector<gc_node_ref_t> normInputs = copy(DraftNodeView::normInputs(draftNode));
    const std::vector<gc_node_ref_t> withInputs = copy(DraftNodeView::withInputs(draftNode));
    const std::vector<gc_node_ref_t> ctrlInputs = copy(DraftNodeView::ctrlInputs(draftNode));
    const std::vector<gc_node_ref_t> normUsers  = copy(usersOf(id, DraftEdgeKind::Norm));
    const std::vector<gc_node_ref_t> withUsers  = copy(usersOf(id, DraftEdgeKind::With));
    const std::vector<gc_node_ref_t> ctrlUsers  = copy(usersOf(id, DraftEdgeKind::Ctrl));

    for (gc_node_ref_t input : normInputs) {
        removeUserRef(input, id, true, false, false);
    }
    for (gc_node_ref_t input : withInputs) {
        removeUserRef(input, id, false, true, false);
    }
    for (gc_node_ref_t input : ctrlInputs) {
        removeUserRef(input, id, false, false, true);
    }
    // Whoever reads the node stops reading it, as often as it did. The lists of users are
    // kept in step with the inputs, so nothing else needs looking at.
    const auto unlinkAll = [&](DraftEdgeKind kind, const std::vector<gc_node_ref_t> &users) {
        for (gc_node_ref_t userId : users) {
            if (userId != id && alive(userId)) {
                while (unlinkInput(kind, userId, id)) {
                }
            }
        }
    };
    unlinkAll(DraftEdgeKind::Norm, normUsers);
    unlinkAll(DraftEdgeKind::With, withUsers);
    unlinkAll(DraftEdgeKind::Ctrl, ctrlUsers);
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
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set norm inputs for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::normInputs(draftNode)) {
        removeUserRef(oldInput, id, true, false, false);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.normInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, true, false, false);
    }
}

void GraphDraft::setWithInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set with inputs for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::withInputs(draftNode)) {
        removeUserRef(oldInput, id, false, true, false);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.withInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, false, true, false);
    }
}

void GraphDraft::setCtrlInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs) {
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot set ctrl inputs for a missing draft node.");
    for (gc_node_ref_t oldInput : DraftNodeView::ctrlInputs(draftNode)) {
        removeUserRef(oldInput, id, false, false, true);
    }

    DraftNodeInit init = makeInitFromNode(draftNode);
    init.ctrlInputs    = inputs;
    replaceNodeStorage(id, rebuildNode(id, init));

    for (gc_node_ref_t input : inputs) {
        appendUserRef(input, id, false, false, true);
    }
}

void GraphDraft::replaceUsesInList(
    gc_node_ref_t ownerId, std::span<const gc_node_ref_t> users, gc_node_ref_t oldId,
    gc_node_ref_t newId, bool norm, bool with, bool ctrl) {
    ++mutations_;
    for (gc_node_ref_t userId : users) {
        DraftNode *userNode = node(userId);
        if (userNode == nullptr) {
            continue;
        }
        DraftNodeInit init = makeInitFromNode(userNode);
        std::vector<gc_node_ref_t> normInputs(init.normInputs.begin(), init.normInputs.end());
        std::vector<gc_node_ref_t> withInputs(init.withInputs.begin(), init.withInputs.end());
        std::vector<gc_node_ref_t> ctrlInputs(init.ctrlInputs.begin(), init.ctrlInputs.end());
        auto replaceAll = [oldId, newId](std::vector<gc_node_ref_t> &items) {
            for (gc_node_ref_t &item : items) {
                if (item == oldId) {
                    item = newId;
                }
            }
        };
        if (norm)
            replaceAll(normInputs);
        if (with)
            replaceAll(withInputs);
        if (ctrl)
            replaceAll(ctrlInputs);
        init.normInputs = normInputs;
        init.withInputs = withInputs;
        init.ctrlInputs = ctrlInputs;
        replaceNodeStorage(userId, rebuildNode(userId, init));
        removeUserRef(ownerId, userId, norm, with, ctrl);
        appendUserRef(newId, userId, norm, with, ctrl);
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
        replaceOne(payload->defaultArm);
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

void GraphDraft::replaceAllNormUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    std::vector<gc_node_ref_t> users(normUsersOf(oldId).begin(), normUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, true, false, false);
    replaceValueStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllWithUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    std::vector<gc_node_ref_t> users(withUsersOf(oldId).begin(), withUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, false, true, false);
    replaceValueStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllCtrlUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    std::vector<gc_node_ref_t> users(ctrlUsersOf(oldId).begin(), ctrlUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, false, false, true);
    replaceCtrlStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllValueUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    ++mutations_;
    replaceAllNormUses(oldId, newId);
    replaceAllWithUses(oldId, newId);
}

void GraphDraft::retargetBranchArmAnchors(
    gc_node_ref_t oldId, gc_node_ref_t newHeadId, gc_node_ref_t newTailId) {
    ++mutations_;
    for (gc_node_ref_t id : brchIds_) {
        DraftNode *draftNode = node(id);
        if (!draftNode || draftNode->header.kind != GCNodeKind::Brch) {
            continue;
        }
        auto *payload = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
        auto *arms    = reinterpret_cast<GCBranchArm *>(
            DraftNodeView::payload(draftNode).data() + sizeof(DraftBrchPayload));
        for (size_t i = 0; i < payload->armCount; ++i) {
            if (arms[i].head == oldId) {
                arms[i].head = newHeadId;
            }
            if (arms[i].tail == oldId) {
                arms[i].tail = newTailId;
            }
        }
    }
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
            if (!h || h->kind != GCNodeKind::Gate || !ctrlInputsOf(id).empty() ||
                normInputsOf(id).empty() || isBranchArmAnchor(id)) {
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
    // A GATE that is the head or tail of a branch arm is not dissolved: it marks where the arm
    // starts or ends. It can still lose every control input, when all it waited on were gates
    // dissolved above or parameters of an inlined callee (an inlined function whose exit GATE
    // only waited on its ports and on gated constants, placed as the tail of an arm). Such a
    // gate then waits on its BRCH, the shape of the bridge GATE inlining gives an arm whose
    // body has no control entry; a GATE without control input is not a valid graph.
    std::ranges::sort(brchIds_);
    brchIds_.erase(std::ranges::unique(brchIds_).begin(), brchIds_.end());
    for (gc_node_ref_t brch : brchIds_) {
        const DraftNodeHeader *h = header(brch);
        if (!h || h->kind != GCNodeKind::Brch) {
            continue;
        }
        const std::vector<GCBranchArm> arms(branchArmsOf(brch).begin(), branchArmsOf(brch).end());
        for (const GCBranchArm &arm : arms) {
            for (gc_node_ref_t anchor : {arm.head, arm.tail}) {
                const DraftNodeHeader *a = anchor != kInvalidNodeRef ? header(anchor) : nullptr;
                if (a && a->kind == GCNodeKind::Gate && ctrlInputsOf(anchor).empty()) {
                    appendInput(DraftEdgeKind::Ctrl, anchor, brch);
                }
            }
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
    runtimeSlotTypes_.push_back(type);
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
    ++mutations_;
    ASSERT(index < staticSlots_.size(), "Draft static slot index is out of range.");
    touchRefs();
    staticSlots_[index] = value;
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
        .collectionCount = ref->collectionCount,
    };
}

void GraphDraft::setLoopGraphs(gc_node_ref_t id, GraphId cond, GraphId body) {
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
        payloadShape,
        [&](GCGraphPayloadArena &payload) { emitDraftNativePayload(*this, payload); },
        staticSlots_);
}

} // namespace camel::runtime
