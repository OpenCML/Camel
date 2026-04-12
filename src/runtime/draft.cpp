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
 * Updated: Apr. 12, 2026
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
#include <stdexcept>

namespace camel::runtime {

namespace mm = camel::core::mm;

namespace {

constexpr size_t kDraftNodeBytes = sizeof(DraftNode);

size_t totalBytesForTail(size_t tailBytes) { return kDraftNodeBytes + tailBytes; }

uint16_t capacityBytesForStorageClass(DraftNodeStorageClass storageClass, size_t tailBytes) {
    switch (storageClass) {
    case DraftNodeStorageClass::Slab64:
        return static_cast<uint16_t>(64 - sizeof(DraftNode));
    case DraftNodeStorageClass::Slab96:
        return static_cast<uint16_t>(96 - sizeof(DraftNode));
    case DraftNodeStorageClass::Slab128:
        return static_cast<uint16_t>(128 - sizeof(DraftNode));
    case DraftNodeStorageClass::Oversize:
        return static_cast<uint16_t>(
            std::min<size_t>(tailBytes, std::numeric_limits<uint16_t>::max()));
    }
    return 0;
}

size_t adjacencyBytes(
    gc_cnt_t normInputs, gc_cnt_t withInputs, gc_cnt_t ctrlInputs, gc_cnt_t normUsers,
    gc_cnt_t withUsers, gc_cnt_t ctrlUsers) {
    return (static_cast<size_t>(normInputs) + static_cast<size_t>(withInputs) +
            static_cast<size_t>(ctrlInputs) + static_cast<size_t>(normUsers) +
            static_cast<size_t>(withUsers) + static_cast<size_t>(ctrlUsers)) *
           sizeof(gc_node_ref_t);
}

camel::core::type::Type *resolveDraftNodeDataType(const GCGraph *graph, const GCNode *sourceNode) {
    ASSERT(graph != nullptr, "Draft node type resolution requires a non-null graph.");
    ASSERT(sourceNode != nullptr, "Draft node type resolution requires a non-null source node.");
    if (sourceNode->dataType != nullptr) {
        return sourceNode->dataType;
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
    init.payload      = DraftNodeView::payload(node);
    init.normInputs   = DraftNodeView::normInputs(node);
    init.withInputs   = DraftNodeView::withInputs(node);
    init.ctrlInputs   = DraftNodeView::ctrlInputs(node);
    init.normUsers    = DraftNodeView::normUsers(node);
    init.withUsers    = DraftNodeView::withUsers(node);
    init.ctrlUsers    = DraftNodeView::ctrlUsers(node);
    return init;
}

std::byte *tailBytes(DraftNode *node) {
    return reinterpret_cast<std::byte *>(node) + sizeof(DraftNode);
}

const std::byte *tailBytes(const DraftNode *node) {
    return reinterpret_cast<const std::byte *>(node) + sizeof(DraftNode);
}

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

std::span<gc_node_ref_t> DraftNodeView::normUsers(DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount) +
                  static_cast<size_t>(node->header.ctrlInputCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.normUserCount);
}

std::span<const gc_node_ref_t> DraftNodeView::normUsers(const DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount) +
                  static_cast<size_t>(node->header.ctrlInputCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.normUserCount);
}

std::span<gc_node_ref_t> DraftNodeView::withUsers(DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount) +
                  static_cast<size_t>(node->header.ctrlInputCount) +
                  static_cast<size_t>(node->header.normUserCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.withUserCount);
}

std::span<const gc_node_ref_t> DraftNodeView::withUsers(const DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount) +
                  static_cast<size_t>(node->header.ctrlInputCount) +
                  static_cast<size_t>(node->header.normUserCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.withUserCount);
}

std::span<gc_node_ref_t> DraftNodeView::ctrlUsers(DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount) +
                  static_cast<size_t>(node->header.ctrlInputCount) +
                  static_cast<size_t>(node->header.normUserCount) +
                  static_cast<size_t>(node->header.withUserCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.ctrlUserCount);
}

std::span<const gc_node_ref_t> DraftNodeView::ctrlUsers(const DraftNode *node) {
    auto bytes = tailBytes(node) + node->header.payloadBytes +
                 (static_cast<size_t>(node->header.normInputCount) +
                  static_cast<size_t>(node->header.withInputCount) +
                  static_cast<size_t>(node->header.ctrlInputCount) +
                  static_cast<size_t>(node->header.normUserCount) +
                  static_cast<size_t>(node->header.withUserCount)) *
                     sizeof(gc_node_ref_t);
    return edgeSpan(bytes, node->header.ctrlUserCount);
}

DraftNodePool::~DraftNodePool() { clear(); }

size_t DraftNodePool::capacityForStorageClass(DraftNodeStorageClass storageClass) {
    switch (storageClass) {
    case DraftNodeStorageClass::Slab64:
        return 64 - sizeof(DraftNode);
    case DraftNodeStorageClass::Slab96:
        return 96 - sizeof(DraftNode);
    case DraftNodeStorageClass::Slab128:
        return 128 - sizeof(DraftNode);
    case DraftNodeStorageClass::Oversize:
        return 0;
    }
    return 0;
}

DraftNodeStorageClass DraftNodePool::classify(size_t totalBytes) {
    if (totalBytes <= 64) {
        return DraftNodeStorageClass::Slab64;
    }
    if (totalBytes <= 96) {
        return DraftNodeStorageClass::Slab96;
    }
    if (totalBytes <= 128) {
        return DraftNodeStorageClass::Slab128;
    }
    return DraftNodeStorageClass::Oversize;
}

DraftNode *DraftNodePool::allocFromArena(SlabArena &arena) {
    const size_t blocksPerPage = std::max<size_t>(1, kPageBytes / arena.blockBytes);
    if (arena.pages.empty() || arena.cursor >= blocksPerPage) {
        arena.pages.push_back(std::make_unique<std::byte[]>(arena.blockBytes * blocksPerPage));
        arena.cursor = 0;
    }
    std::byte *base = arena.pages.back().get() + arena.cursor * arena.blockBytes;
    arena.cursor++;
    std::memset(base, 0, arena.blockBytes);
    return reinterpret_cast<DraftNode *>(base);
}

DraftNode *DraftNodePool::alloc(size_t tailBytes, DraftNodeStorageClass *storageClass) {
    const size_t totalBytes         = totalBytesForTail(tailBytes);
    const auto resolvedStorageClass = classify(totalBytes);
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
        void *mem = std::malloc(totalBytes);
        if (mem == nullptr) {
            throw std::bad_alloc{};
        }
        std::memset(mem, 0, totalBytes);
        oversizeBlocks_.push_back(mem);
        return reinterpret_cast<DraftNode *>(mem);
    }
    }

    throw std::logic_error("Unreachable draft node storage class.");
}

DraftNode *DraftNodePool::realloc(
    DraftNode *node, size_t oldTailBytes, size_t newTailBytes,
    DraftNodeStorageClass *storageClass) {
    DraftNode *replacement = alloc(newTailBytes, storageClass);
    std::memcpy(
        replacement,
        node,
        std::min(totalBytesForTail(oldTailBytes), totalBytesForTail(newTailBytes)));
    return replacement;
}

void DraftNodePool::clear() {
    slab64_.pages.clear();
    slab96_.pages.clear();
    slab128_.pages.clear();
    slab64_.cursor  = 0;
    slab96_.cursor  = 0;
    slab128_.cursor = 0;
    for (void *block : oversizeBlocks_) {
        std::free(block);
    }
    oversizeBlocks_.clear();
}

bool GraphDraft::containsNode(gc_node_ref_t id) const {
    return id < nodesById_.size() && nodesById_[id] != nullptr;
}

bool GraphDraft::alive(gc_node_ref_t id) const { return node(id) != nullptr; }

const DraftNodeHeader *GraphDraft::header(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? &draftNode->header : nullptr;
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

const DraftNode *GraphDraft::node(gc_node_ref_t id) const {
    if (id >= nodesById_.size()) {
        return nullptr;
    }
    return nodesById_[id];
}

DraftNode *GraphDraft::node(gc_node_ref_t id) {
    if (id >= nodesById_.size()) {
        return nullptr;
    }
    return nodesById_[id];
}

std::span<const gc_node_ref_t> GraphDraft::normInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::normInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::withInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::withInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlInputsOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::ctrlInputs(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::normUsersOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::normUsers(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::withUsersOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::withUsers(draftNode) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GraphDraft::ctrlUsersOf(gc_node_ref_t id) const {
    const DraftNode *draftNode = node(id);
    return draftNode ? DraftNodeView::ctrlUsers(draftNode) : std::span<const gc_node_ref_t>{};
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

bool GraphDraft::isBranchArmAnchor(gc_node_ref_t id) const {
    for (gc_node_ref_t draftId = 0; draftId < nodeSlotCount(); ++draftId) {
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
    return sourceNode->bodyBytes();
}

DraftNode *GraphDraft::createDecodedNode(
    const GCGraph *graph, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &sourceToDraft) {
    const GCNode *sourceNode = graph->node(sourceRef);
    ASSERT(sourceNode != nullptr, "Draft decode encountered a null source node.");

    const auto normInputs = graph->normInputsOf(sourceRef);
    const auto withInputs = graph->withInputsOf(sourceRef);
    const auto ctrlInputs = graph->ctrlInputsOf(sourceRef);
    const auto normUsers  = graph->normOutputsOf(sourceRef);
    const auto withUsers  = graph->withOutputsOf(sourceRef);
    const auto ctrlUsers  = graph->ctrlOutputsOf(sourceRef);

    const uint16_t payloadBytes =
        static_cast<uint16_t>(payloadBytesForSourceNode(graph, sourceRef));
    const size_t tailBytes =
        static_cast<size_t>(payloadBytes) + adjacencyBytes(
                                                static_cast<gc_cnt_t>(normInputs.size()),
                                                static_cast<gc_cnt_t>(withInputs.size()),
                                                static_cast<gc_cnt_t>(ctrlInputs.size()),
                                                static_cast<gc_cnt_t>(normUsers.size()),
                                                static_cast<gc_cnt_t>(withUsers.size()),
                                                static_cast<gc_cnt_t>(ctrlUsers.size()));

    DraftNodeStorageClass storageClass = DraftNodeStorageClass::Oversize;
    DraftNode *draftNode               = pool_.alloc(tailBytes, &storageClass);
    draftNode->header.dataIndex        = sourceNode->dataIndex;
    draftNode->header.payloadBytes     = payloadBytes;
    draftNode->header.capacityBytes    = capacityBytesForStorageClass(storageClass, tailBytes);
    draftNode->header.dataType         = resolveDraftNodeDataType(graph, sourceNode);
    draftNode->header.normInputCount   = static_cast<gc_cnt_t>(normInputs.size());
    draftNode->header.withInputCount   = static_cast<gc_cnt_t>(withInputs.size());
    draftNode->header.ctrlInputCount   = static_cast<gc_cnt_t>(ctrlInputs.size());
    draftNode->header.normUserCount    = static_cast<gc_cnt_t>(normUsers.size());
    draftNode->header.withUserCount    = static_cast<gc_cnt_t>(withUsers.size());
    draftNode->header.ctrlUserCount    = static_cast<gc_cnt_t>(ctrlUsers.size());
    draftNode->header.kind             = sourceNode->kind;
    draftNode->header.runtimeFlags     = sourceNode->flags;
    draftNode->header.storageCls       = storageClass;

    if (sourceNode->kind == GCNodeKind::Brch) {
        const auto sourceArms         = graph->branchArmsOf(sourceRef);
        auto *payload                 = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
        const auto *sourceBrchPayload = sourceNode->bodyAs<GCBrchBody>();
        payload->join                 = translateNodeRef(sourceBrchPayload->join, sourceToDraft);
        payload->armCount             = sourceBrchPayload->armCount;
        payload->defaultArm = translateNodeRef(sourceBrchPayload->defaultArm, sourceToDraft);
        auto *armBytes      = reinterpret_cast<GCBranchArm *>(
            DraftNodeView::payload(draftNode).data() + sizeof(DraftBrchPayload));
        for (size_t i = 0; i < sourceArms.size(); ++i) {
            armBytes[i].head = translateNodeRef(sourceArms[i].head, sourceToDraft);
            armBytes[i].tail = translateNodeRef(sourceArms[i].tail, sourceToDraft);
        }
    } else if (sourceNode->bodyBytes() != 0) {
        std::memcpy(
            DraftNodeView::payload(draftNode).data(),
            reinterpret_cast<const std::byte *>(sourceNode) + sizeof(GCNode),
            sourceNode->bodyBytes());
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
    copyTranslated(DraftNodeView::normUsers(draftNode), normUsers);
    copyTranslated(DraftNodeView::withUsers(draftNode), withUsers);
    copyTranslated(DraftNodeView::ctrlUsers(draftNode), ctrlUsers);

    return draftNode;
}

std::unique_ptr<GraphDraft> GraphDraft::decode(const GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph draft decode requires a non-null graph.");
    CAMEL_LOG_INFO_S(
        "GraphDraft",
        "Decode begin for graph '{}' with {} nodes.",
        graph->name(),
        graph->nodeCount());
    auto draft              = std::make_unique<GraphDraft>();
    draft->funcType_        = graph->funcType();
    draft->runtimeDataType_ = const_cast<camel::core::type::TupleType *>(graph->runtimeDataType());
    draft->closureType_     = const_cast<camel::core::type::TupleType *>(graph->closureType());
    draft->returnKind_      = graph->returnKind();
    draft->staticSlots_.assign(graph->staticSlots().begin(), graph->staticSlots().end());
    if (const auto *staticType = graph->staticDataType()) {
        draft->staticSlotTypes_.assign(staticType->types().begin(), staticType->types().end());
    } else {
        draft->staticSlotTypes_.clear();
    }
    draft->dependencies_.assign(graph->dependencies().begin(), graph->dependencies().end());
    draft->subGraphs_.assign(graph->subGraphs().begin(), graph->subGraphs().end());
    draft->staticGraphRefs_.assign(
        graph->staticGraphRefs().begin(),
        graph->staticGraphRefs().end());
    draft->draftIdsBySourceRef_.assign(graph->nodeBlockCount(), kInvalidNodeRef);

    std::vector<gc_node_ref_t> sourceToDraft(graph->nodeBlockCount(), kInvalidNodeRef);
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
    return draft;
}

DraftNode *GraphDraft::rebuildNode(gc_node_ref_t id, const DraftNodeInit &init) {
    const size_t tailBytes =
        init.payload.size_bytes() + adjacencyBytes(
                                        static_cast<gc_cnt_t>(init.normInputs.size()),
                                        static_cast<gc_cnt_t>(init.withInputs.size()),
                                        static_cast<gc_cnt_t>(init.ctrlInputs.size()),
                                        static_cast<gc_cnt_t>(init.normUsers.size()),
                                        static_cast<gc_cnt_t>(init.withUsers.size()),
                                        static_cast<gc_cnt_t>(init.ctrlUsers.size()));

    DraftNodeStorageClass storageClass = DraftNodeStorageClass::Oversize;
    DraftNode *draftNode               = pool_.alloc(tailBytes, &storageClass);
    draftNode->header.dataIndex        = init.dataIndex;
    draftNode->header.payloadBytes     = static_cast<uint16_t>(
        std::min<size_t>(init.payload.size_bytes(), std::numeric_limits<uint16_t>::max()));
    draftNode->header.capacityBytes  = capacityBytesForStorageClass(storageClass, tailBytes);
    draftNode->header.dataType       = init.dataType;
    draftNode->header.normInputCount = static_cast<gc_cnt_t>(init.normInputs.size());
    draftNode->header.withInputCount = static_cast<gc_cnt_t>(init.withInputs.size());
    draftNode->header.ctrlInputCount = static_cast<gc_cnt_t>(init.ctrlInputs.size());
    draftNode->header.normUserCount  = static_cast<gc_cnt_t>(init.normUsers.size());
    draftNode->header.withUserCount  = static_cast<gc_cnt_t>(init.withUsers.size());
    draftNode->header.ctrlUserCount  = static_cast<gc_cnt_t>(init.ctrlUsers.size());
    draftNode->header.kind           = init.kind;
    draftNode->header.runtimeFlags   = init.runtimeFlags;
    draftNode->header.storageCls     = storageClass;

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
    if (!init.normUsers.empty()) {
        std::ranges::copy(init.normUsers, DraftNodeView::normUsers(draftNode).begin());
    }
    if (!init.withUsers.empty()) {
        std::ranges::copy(init.withUsers, DraftNodeView::withUsers(draftNode).begin());
    }
    if (!init.ctrlUsers.empty()) {
        std::ranges::copy(init.ctrlUsers, DraftNodeView::ctrlUsers(draftNode).begin());
    }

    (void)id;
    return draftNode;
}

void GraphDraft::replaceNodeStorage(gc_node_ref_t id, DraftNode *nodePtr) {
    ASSERT(id < nodesById_.size(), "Draft node storage replacement id is out of range.");
    nodesById_[id] = nodePtr;
}

void GraphDraft::removeUserRef(
    gc_node_ref_t id, gc_node_ref_t userId, bool norm, bool with, bool ctrl) {
    DraftNode *draftNode = node(id);
    if (draftNode == nullptr) {
        return;
    }
    DraftNodeInit init = makeInitFromNode(draftNode);
    std::vector<gc_node_ref_t> normUsers(init.normUsers.begin(), init.normUsers.end());
    std::vector<gc_node_ref_t> withUsers(init.withUsers.begin(), init.withUsers.end());
    std::vector<gc_node_ref_t> ctrlUsers(init.ctrlUsers.begin(), init.ctrlUsers.end());

    auto eraseOne = [userId](std::vector<gc_node_ref_t> &items) {
        auto it = std::find(items.begin(), items.end(), userId);
        if (it != items.end()) {
            items.erase(it);
        }
    };
    if (norm)
        eraseOne(normUsers);
    if (with)
        eraseOne(withUsers);
    if (ctrl)
        eraseOne(ctrlUsers);
    init.normUsers = normUsers;
    init.withUsers = withUsers;
    init.ctrlUsers = ctrlUsers;
    replaceNodeStorage(id, rebuildNode(id, init));
}

void GraphDraft::appendUserRef(
    gc_node_ref_t id, gc_node_ref_t userId, bool norm, bool with, bool ctrl) {
    DraftNode *draftNode = node(id);
    if (draftNode == nullptr) {
        return;
    }
    DraftNodeInit init = makeInitFromNode(draftNode);
    std::vector<gc_node_ref_t> normUsers(init.normUsers.begin(), init.normUsers.end());
    std::vector<gc_node_ref_t> withUsers(init.withUsers.begin(), init.withUsers.end());
    std::vector<gc_node_ref_t> ctrlUsers(init.ctrlUsers.begin(), init.ctrlUsers.end());

    auto appendUnique = [userId](std::vector<gc_node_ref_t> &items) {
        if (std::find(items.begin(), items.end(), userId) == items.end()) {
            items.push_back(userId);
        }
    };
    if (norm)
        appendUnique(normUsers);
    if (with)
        appendUnique(withUsers);
    if (ctrl)
        appendUnique(ctrlUsers);
    init.normUsers = normUsers;
    init.withUsers = withUsers;
    init.ctrlUsers = ctrlUsers;
    replaceNodeStorage(id, rebuildNode(id, init));
}

gc_node_ref_t GraphDraft::addNode(const DraftNodeInit &init) {
    ASSERT(
        nodesById_.size() < std::numeric_limits<gc_node_ref_t>::max(),
        "GraphDraft node count exceeds 16-bit id capacity.");
    gc_node_ref_t id = static_cast<gc_node_ref_t>(nodesById_.size());
    nodesById_.push_back(rebuildNode(id, init));
    sourceRefsById_.push_back(kInvalidNodeRef);
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

void GraphDraft::eraseNode(gc_node_ref_t id) {
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot erase a missing draft node.");

    const std::vector<gc_node_ref_t> normInputs(
        DraftNodeView::normInputs(draftNode).begin(),
        DraftNodeView::normInputs(draftNode).end());
    const std::vector<gc_node_ref_t> withInputs(
        DraftNodeView::withInputs(draftNode).begin(),
        DraftNodeView::withInputs(draftNode).end());
    const std::vector<gc_node_ref_t> ctrlInputs(
        DraftNodeView::ctrlInputs(draftNode).begin(),
        DraftNodeView::ctrlInputs(draftNode).end());
    const std::vector<gc_node_ref_t> normUsers(
        DraftNodeView::normUsers(draftNode).begin(),
        DraftNodeView::normUsers(draftNode).end());
    const std::vector<gc_node_ref_t> withUsers(
        DraftNodeView::withUsers(draftNode).begin(),
        DraftNodeView::withUsers(draftNode).end());
    const std::vector<gc_node_ref_t> ctrlUsers(
        DraftNodeView::ctrlUsers(draftNode).begin(),
        DraftNodeView::ctrlUsers(draftNode).end());

    for (gc_node_ref_t input : normInputs) {
        removeUserRef(input, id, true, false, false);
    }
    for (gc_node_ref_t input : withInputs) {
        removeUserRef(input, id, false, true, false);
    }
    for (gc_node_ref_t input : ctrlInputs) {
        removeUserRef(input, id, false, false, true);
    }
    for (gc_node_ref_t userId : normUsers) {
        (void)unlinkInput(DraftEdgeKind::Norm, userId, id);
    }
    for (gc_node_ref_t userId : withUsers) {
        (void)unlinkInput(DraftEdgeKind::With, userId, id);
    }
    for (gc_node_ref_t userId : ctrlUsers) {
        (void)unlinkInput(DraftEdgeKind::Ctrl, userId, id);
    }
    for (gc_node_ref_t otherId = 0; otherId < nodeSlotCount(); ++otherId) {
        if (otherId == id || !alive(otherId)) {
            continue;
        }
        while (std::find(normInputsOf(otherId).begin(), normInputsOf(otherId).end(), id) !=
               normInputsOf(otherId).end()) {
            (void)unlinkInput(DraftEdgeKind::Norm, otherId, id);
        }
        while (std::find(withInputsOf(otherId).begin(), withInputsOf(otherId).end(), id) !=
               withInputsOf(otherId).end()) {
            (void)unlinkInput(DraftEdgeKind::With, otherId, id);
        }
        while (std::find(ctrlInputsOf(otherId).begin(), ctrlInputsOf(otherId).end(), id) !=
               ctrlInputsOf(otherId).end()) {
            (void)unlinkInput(DraftEdgeKind::Ctrl, otherId, id);
        }
    }

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
    auto replaceOne = [&](gc_node_ref_t &ref) {
        if (ref == oldId) {
            ref = newId;
        }
    };

    replaceOne(output_);
    replaceOne(returnNode_);

    for (gc_node_ref_t id = 0; id < nodeSlotCount(); ++id) {
        DraftNode *draftNode = node(id);
        if (!draftNode) {
            continue;
        }
    }
}

void GraphDraft::replaceCtrlStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId) {
    auto replaceOne = [&](gc_node_ref_t &ref) {
        if (ref == oldId) {
            ref = newId;
        }
    };

    replaceOne(entry_);
    replaceOne(exit_);

    for (gc_node_ref_t id = 0; id < nodeSlotCount(); ++id) {
        DraftNode *draftNode = node(id);
        if (!draftNode) {
            continue;
        }
        if (draftNode->header.kind == GCNodeKind::Brch) {
            auto *payload = DraftNodeView::payloadAs<DraftBrchPayload>(draftNode);
            replaceOne(payload->join);
            replaceOne(payload->defaultArm);
            continue;
        }
        if (draftNode->header.kind == GCNodeKind::Join &&
            draftNode->header.payloadBytes >= sizeof(GCJoinBody)) {
            auto *payload = DraftNodeView::payloadAs<GCJoinBody>(draftNode);
            replaceOne(payload->brch);
        }
    }
}

void GraphDraft::replaceAllNormUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    std::vector<gc_node_ref_t> users(normUsersOf(oldId).begin(), normUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, true, false, false);
    replaceValueStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllWithUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    std::vector<gc_node_ref_t> users(withUsersOf(oldId).begin(), withUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, false, true, false);
    replaceValueStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllCtrlUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    std::vector<gc_node_ref_t> users(ctrlUsersOf(oldId).begin(), ctrlUsersOf(oldId).end());
    replaceUsesInList(oldId, users, oldId, newId, false, false, true);
    replaceCtrlStructuralRefs(oldId, newId);
}

void GraphDraft::replaceAllValueUses(gc_node_ref_t oldId, gc_node_ref_t newId) {
    replaceAllNormUses(oldId, newId);
    replaceAllWithUses(oldId, newId);
}

void GraphDraft::retargetBranchArmAnchors(
    gc_node_ref_t oldId, gc_node_ref_t newHeadId, gc_node_ref_t newTailId) {
    for (gc_node_ref_t id = 0; id < nodeSlotCount(); ++id) {
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

gc_slot_idx_t GraphDraft::allocateRuntimeSlot(camel::core::type::Type *type) {
    ASSERT(type != nullptr, "Draft runtime slot allocation requires a non-null type.");
    CAMEL_LOG_INFO_S(
        "GraphDraft",
        "Allocate runtime slot type={} currentRuntimeTuple={}.",
        static_cast<const void *>(type),
        static_cast<const void *>(runtimeDataType_));

    std::vector<camel::core::type::Type *> slotTypes;
    if (runtimeDataType_ != nullptr) {
        const auto currentTypes = runtimeDataType_->types();
        CAMEL_LOG_INFO_S(
            "GraphDraft",
            "Allocate runtime slot existing tuple size={}.",
            currentTypes.size());
        slotTypes.assign(currentTypes.begin(), currentTypes.end());
    }
    if (slotTypes.empty()) {
        slotTypes.push_back(camel::core::type::Type::Void());
    }

    ASSERT(
        slotTypes.size() < static_cast<size_t>(std::numeric_limits<gc_slot_idx_t>::max()),
        "Draft runtime slot count exceeds 16-bit data-index capacity.");
    const gc_slot_idx_t slotIndex = static_cast<gc_slot_idx_t>(slotTypes.size());
    slotTypes.push_back(type);
    CAMEL_LOG_INFO_S(
        "GraphDraft",
        "Allocate runtime slot rebuilding tuple newSize={}.",
        slotTypes.size());
    runtimeDataType_ = camel::core::type::TupleType::create(std::move(slotTypes));
    CAMEL_LOG_INFO_S(
        "GraphDraft",
        "Allocate runtime slot finished index={} newRuntimeTuple={}.",
        slotIndex,
        static_cast<const void *>(runtimeDataType_));
    return slotIndex;
}

size_t GraphDraft::appendStaticSlot(slot_t value, camel::core::type::Type *type) {
    if (staticSlots_.empty()) {
        staticSlots_.push_back(NullSlot);
        staticSlotTypes_.push_back(camel::core::type::Type::Void());
    }
    staticSlots_.push_back(value);
    staticSlotTypes_.push_back(type);
    return staticSlots_.size() - 1;
}

gc_node_ref_t GraphDraft::materializeStaticValue(
    slot_t value, camel::core::type::Type *type, uint8_t runtimeFlags) {
    const size_t slotIndex = appendStaticSlot(value, type);
    ASSERT(
        slotIndex <= static_cast<size_t>(std::numeric_limits<gc_slot_idx_t>::max()),
        "Draft static slot index exceeds 16-bit data-index capacity.");
    DraftNodeInit init{
        .dataIndex    = static_cast<gc_slot_idx_t>(-static_cast<int32_t>(slotIndex)),
        .dataType     = type,
        .kind         = GCNodeKind::Data,
        .runtimeFlags = static_cast<uint8_t>(runtimeFlags | kGCNodeFlagConstant),
    };
    return addNode(init);
}

void GraphDraft::setStaticSlot(size_t index, slot_t value) {
    ASSERT(index < staticSlots_.size(), "Draft static slot index is out of range.");
    staticSlots_[index] = value;
}

void GraphDraft::addDependency(GCGraph *graph) {
    if (graph &&
        std::find(dependencies_.begin(), dependencies_.end(), graph) == dependencies_.end()) {
        dependencies_.push_back(graph);
    }
}

void GraphDraft::addSubGraph(GCGraph *graph) {
    if (graph && std::find(subGraphs_.begin(), subGraphs_.end(), graph) == subGraphs_.end()) {
        subGraphs_.push_back(graph);
    }
}

void GraphDraft::addStaticGraphRef(GCGraph *graph) {
    if (graph && std::find(staticGraphRefs_.begin(), staticGraphRefs_.end(), graph) ==
                     staticGraphRefs_.end()) {
        staticGraphRefs_.push_back(graph);
    }
}

GCGraph *GraphDraft::encode(
    const std::string &stableId, const std::string &mangledName, const std::string &name) const {
    auto *debugRecord = createGraphDebugRecord(stableId, mangledName, name);
    camel::core::type::TupleType *staticDataType = nullptr;
    if (!staticSlotTypes_.empty()) {
        staticDataType = camel::core::type::TupleType::create(staticSlotTypes_);
    }
    const auto payloadShape = describeDraftNativePayload(*this);

    return GCGraphBuildAccess::create(
        debugRecord,
        funcType_,
        runtimeDataType_,
        staticDataType,
        closureType_,
        nullptr,
        dependencies_,
        subGraphs_,
        staticGraphRefs_,
        payloadShape,
        [&](GCGraphPayloadArena &payload) { emitDraftNativePayload(*this, payload); },
        staticSlots_);
}

} // namespace camel::runtime
