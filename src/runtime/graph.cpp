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
 * Created: Apr. 07, 2026
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime graph allocation, native payload storage, and payload accessors.
 *
 * This file targets
 * the final runtime layout rather than the earlier
 * bridge-style node-record arrays. The payload
 * keeps one variable-length node
 * blob, one unified edge arena, and a compact branch-arm arena.

 */

#include "camel/runtime/graph.h"
#include "camel/runtime/draft.h"
#include "runtime/graph_build.h"

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

namespace camel::runtime {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
namespace mm = camel::core::mm;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

namespace {

struct FixedBufferAllocator final : camel::core::mm::IAllocator {
    FixedBufferAllocator(void *base, size_t size)
        : base_(reinterpret_cast<std::byte *>(base)), cursor_(base_), end_(base_ + size) {}

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        const size_t aligned =
            camel::core::mm::alignUp(static_cast<size_t>(cursor_ - base_), align);
        std::byte *next = base_ + aligned;
        if (next + size > end_) {
            return nullptr;
        }
        cursor_ = next + size;
        return next;
    }

    void free(void *ptr) override { (void)ptr; }
    bool contains(void *ptr) const override {
        auto *raw = reinterpret_cast<std::byte *>(ptr);
        return raw >= base_ && raw < end_;
    }
    size_t available() const override { return static_cast<size_t>(end_ - cursor_); }

  private:
    std::byte *base_   = nullptr;
    std::byte *cursor_ = nullptr;
    std::byte *end_    = nullptr;
};

template <typename T> size_t bytesForArray(size_t count) {
    return count == 0 ? 0 : sizeof(T) * count;
}

Type *effectiveDraftNodeType(const GraphDraft &draft, const DraftNodeHeader &header) {
    if (header.dataType != nullptr) {
        return header.dataType;
    }
    if (header.dataIndex > 0) {
        if (auto *runtimeType = draft.runtimeDataType();
            runtimeType != nullptr && static_cast<size_t>(header.dataIndex) < runtimeType->size()) {
            return runtimeType->typeAt(static_cast<size_t>(header.dataIndex));
        }
    } else if (header.dataIndex < 0) {
        const size_t staticIndex = static_cast<size_t>(-header.dataIndex);
        const auto staticTypes   = draft.staticSlotTypes();
        if (staticIndex < staticTypes.size()) {
            return staticTypes[staticIndex];
        }
    }
    return nullptr;
}

struct GraphArenaLayout {
    size_t graphBytes          = 0;
    size_t dependencyBytes     = 0;
    size_t subGraphBytes       = 0;
    size_t staticGraphRefBytes = 0;
    size_t tupleBytes          = 0;
    size_t nodeBlockBytes      = 0;
    size_t edgeBytes           = 0;
    size_t normPortBytes       = 0;
    size_t withPortBytes       = 0;
    size_t closureBytes        = 0;
    size_t branchArmBytes      = 0;
    size_t totalBytes          = 0;
};

GraphArenaLayout planGraphArenaLayout(
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payload,
    std::span<const slot_t> staticSlots) {
    GraphArenaLayout layout{
        .graphBytes      = camel::core::mm::alignUp(sizeof(GCGraph), alignof(GCGraph)),
        .dependencyBytes = camel::core::mm::alignUp(
            bytesForArray<GCGraph *>(dependencies.size()),
            alignof(GCGraph *)),
        .subGraphBytes = camel::core::mm::alignUp(
            bytesForArray<GCGraph *>(subGraphs.size()),
            alignof(GCGraph *)),
        .staticGraphRefBytes = camel::core::mm::alignUp(
            bytesForArray<GCGraph *>(staticGraphRefs.size()),
            alignof(GCGraph *)),
        .tupleBytes     = staticSlots.empty()
                              ? 0
                              : camel::core::mm::alignUp(
                                    sizeof(::Tuple) + sizeof(slot_t) * staticSlots.size(),
                                    alignof(::Tuple)),
        .nodeBlockBytes = camel::core::mm::alignUp(
            bytesForArray<gc_block_t>(payload.nodeBlockCount),
            alignof(gc_block_t)),
        .edgeBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.edgeCount),
            alignof(gc_node_ref_t)),
        .normPortBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.normPortCount),
            alignof(gc_node_ref_t)),
        .withPortBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.withPortCount),
            alignof(gc_node_ref_t)),
        .closureBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.closureCount),
            alignof(gc_node_ref_t)),
        .branchArmBytes = camel::core::mm::alignUp(
            bytesForArray<GCBranchArm>(payload.branchArmCount),
            alignof(GCBranchArm)),
    };
    layout.totalBytes = layout.graphBytes + layout.dependencyBytes + layout.subGraphBytes +
                        layout.staticGraphRefBytes + layout.tupleBytes + layout.nodeBlockBytes +
                        layout.edgeBytes + layout.normPortBytes + layout.withPortBytes +
                        layout.closureBytes + layout.branchArmBytes;
    return layout;
}

template <typename T> T *arenaAllocArray(FixedBufferAllocator &arena, size_t count) {
    if (count == 0) {
        return nullptr;
    }
    void *mem = arena.alloc(sizeof(T) * count, alignof(T));
    if (!mem) {
        throw std::bad_alloc();
    }
    auto *ptr = reinterpret_cast<T *>(mem);
    std::memset(ptr, 0, sizeof(T) * count);
    return ptr;
}

template <typename T> T *arenaCopyArray(FixedBufferAllocator &arena, std::span<const T> items) {
    if (items.empty()) {
        return nullptr;
    }
    T *dst = arenaAllocArray<T>(arena, items.size());
    std::memcpy(dst, items.data(), sizeof(T) * items.size());
    return dst;
}

template <typename T> void pushUnique(std::vector<T *> &items, T *value) {
    if (value && std::find(items.begin(), items.end(), value) == items.end()) {
        items.push_back(value);
    }
}

template <typename T, typename U> T narrowIntegral(U value, const char *what) {
    if (value > static_cast<U>(std::numeric_limits<T>::max())) {
        throw std::runtime_error(std::format("{} exceeds the target integral width.", what));
    }
    return static_cast<T>(value);
}

void validateRuntimeGraphPayloadImpl(const GCGraph *graph) {
    if (!graph) {
        return;
    }
    if (!graph->hasNodePayload()) {
        throw std::runtime_error(
            std::format("Runtime graph '{}' has no native node payload.", graph->name()));
    }

    const auto validateRef = [&](gc_node_ref_t ref, std::string_view what) {
        if (ref == kInvalidNodeRef) {
            return;
        }
        if (!graph->containsNodeRef(ref)) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has invalid {} node ref {}.",
                    graph->name(),
                    what,
                    ref));
        }
    };

    validateRef(graph->entryNodeRef(), "entry");
    validateRef(graph->exitNodeRef(), "exit");
    validateRef(graph->outputNodeRef(), "output");
    validateRef(graph->returnNodeRef(), "return");
    const size_t runtimeSlotCount = graph->runtimeDataType() ? graph->runtimeDataType()->size() : 0;
    const size_t staticSlotCount  = graph->staticSlots().size();

    for (gc_node_ref_t ref = 0; ref < graph->nodeBlockCount();) {
        const GCNode *node = graph->node(ref);
        if (!node) {
            break;
        }
        if (node->blockCount == 0) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has zero-sized node block at ref {}.",
                    graph->name(),
                    ref));
        }
        if (node->dataIndex > 0 && static_cast<size_t>(node->dataIndex) >= runtimeSlotCount) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has node {} with runtime slot {} outside runtime-data size "
                    "{}.",
                    graph->name(),
                    ref,
                    node->dataIndex,
                    runtimeSlotCount));
        }
        if (node->dataIndex < 0 && static_cast<size_t>(-node->dataIndex) >= staticSlotCount) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has node {} with static slot {} outside static-data size "
                    "{}.",
                    graph->name(),
                    ref,
                    -node->dataIndex,
                    staticSlotCount));
        }
        auto validateSlice = [&](Slice slice, std::string_view edgeKind) {
            const auto refs = graph->edgeSlice(slice);
            if (slice.count != refs.size()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has out-of-range {} slice at ref {}.",
                        graph->name(),
                        edgeKind,
                        ref));
            }
            for (gc_node_ref_t edgeRef : refs) {
                validateRef(edgeRef, edgeKind);
            }
        };
        validateSlice(node->normInputs, "norm-input");
        validateSlice(node->withInputs, "with-input");
        validateSlice(node->ctrlInputs, "ctrl-input");
        validateSlice(node->normOutputs, "norm-output");
        validateSlice(node->withOutputs, "with-output");
        validateSlice(node->ctrlOutputs, "ctrl-output");
        if (node->kind == GCNodeKind::Func) {
            const auto *body = node->bodyAs<GCFuncBody>();
            if (!body || !body->calleeGraph) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has FUNC node {} without callee graph.",
                        graph->name(),
                        ref));
            }
        }
        if (node->kind == GCNodeKind::Call) {
            const auto calleeInputs = graph->withInputsOf(ref);
            if (calleeInputs.empty()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has CALL node {} without callee input.",
                        graph->name(),
                        ref));
            }
        }
        if (node->kind == GCNodeKind::Gate) {
            const auto ctrlInputs = graph->ctrlInputsOf(ref);
            if (ctrlInputs.empty()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has GATE node {} without control input.",
                        graph->name(),
                        ref));
            }
        }
        if (node->kind == GCNodeKind::Brch) {
            const auto *body = node->bodyAs<GCBrchBody>();
            if (!body) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has BRCH node {} without branch payload.",
                        graph->name(),
                        ref));
            }
            const auto selectorInputs = graph->normInputsOf(ref);
            if (selectorInputs.size() != 1) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has BRCH node {} with {} selector inputs.",
                        graph->name(),
                        ref,
                        selectorInputs.size()));
            }
            validateRef(body->join, "branch-join");
            const auto arms = graph->branchArmsOf(ref);
            if (arms.size() != body->armCount) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has BRCH node {} with mismatched arm count: body={}, "
                        "slice={}.",
                        graph->name(),
                        ref,
                        body->armCount,
                        arms.size()));
            }
            for (size_t armIndex = 0; armIndex < arms.size(); ++armIndex) {
                validateRef(arms[armIndex].head, "branch-arm-head");
                validateRef(arms[armIndex].tail, "branch-arm-tail");
            }
        }
        if (node->kind == GCNodeKind::Join) {
            const auto *body = node->bodyAs<GCJoinBody>();
            if (!body) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has JOIN node {} without join payload.",
                        graph->name(),
                        ref));
            }
            if (body->brch != kInvalidNodeRef) {
                validateRef(body->brch, "join-branch");
            }
            const auto armInputs = graph->withInputsOf(ref);
            if (armInputs.size() != body->armCount) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has JOIN node {} with mismatched arm count: body={}, "
                        "inputs={}.",
                        graph->name(),
                        ref,
                        body->armCount,
                        armInputs.size()));
            }
            const auto normInputs = graph->normInputsOf(ref);
            if (normInputs.empty()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has JOIN node {} without branch-index input.",
                        graph->name(),
                        ref));
            }
        }
        ref = graph->nextNodeRef(ref);
    }
}

std::byte *mutableNodeStorage(gc_block_t *blocks, gc_node_ref_t ref) {
    return reinterpret_cast<std::byte *>(blocks + ref);
}

const std::byte *nodeStorage(const gc_block_t *blocks, gc_node_ref_t ref) {
    return reinterpret_cast<const std::byte *>(blocks + ref);
}

template <typename Body> void writeBody(gc_block_t *blocks, gc_node_ref_t ref, const Body &body) {
    static_assert(
        std::is_trivially_copyable_v<Body>,
        "GC node bodies must remain trivially copyable.");
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode);
    std::memcpy(dst, &body, sizeof(Body));
}

std::span<const gc_node_ref_t>
sliceView(const gc_node_ref_t *storage, Slice slice, gc_cnt_t totalCount) {
    if (!storage || slice.count == 0) {
        return {};
    }
    if (static_cast<size_t>(slice.offset) + static_cast<size_t>(slice.count) > totalCount) {
        throw std::runtime_error("GCGraph edge slice is out of bounds.");
    }
    return {storage + slice.offset, slice.count};
}

std::span<const GCBranchArm>
branchSliceView(const GCBranchArm *storage, gc_off_t offset, gc_cnt_t count, gc_cnt_t totalCount) {
    if (!storage || count == 0) {
        return {};
    }
    if (static_cast<size_t>(offset) + static_cast<size_t>(count) > totalCount) {
        throw std::runtime_error("GCGraph branch-arm slice is out of bounds.");
    }
    return {storage + offset, count};
}

struct PlannedDraftNode {
    gc_node_ref_t draftId = kInvalidNodeRef;
    gc_node_ref_t ref     = kInvalidNodeRef;
    gc_cnt_t blockCount   = 0;
};

struct DraftPayloadPlan {
    std::vector<PlannedDraftNode> nodes;
    std::vector<gc_node_ref_t> runtimeRefsByDraftId;
    gc_cnt_t nodeBlockCount = 0;
    gc_cnt_t edgeCount      = 0;
    gc_cnt_t branchArmCount = 0;
};

gc_cnt_t draftNodeBlockCount(const DraftNodeHeader &header) {
    size_t bodyBytes = 0;
    switch (header.kind) {
    case GCNodeKind::Func:
        bodyBytes = sizeof(GCFuncBody);
        break;
    case GCNodeKind::Call:
        bodyBytes = sizeof(GCCallBody);
        break;
    case GCNodeKind::Accs:
    case GCNodeKind::Oper:
        bodyBytes = header.payloadBytes;
        break;
    case GCNodeKind::Fill:
        bodyBytes = sizeof(GCFillBody);
        break;
    case GCNodeKind::Brch:
        bodyBytes = sizeof(GCBrchBody);
        break;
    case GCNodeKind::Join:
        bodyBytes = sizeof(GCJoinBody);
        break;
    default:
        bodyBytes = 0;
        break;
    }
    return blocksForBytes(sizeof(GCNode) + bodyBytes);
}

DraftPayloadPlan planDraftPayload(const GraphDraft &draft) {
    auto requireAlive = [&](gc_node_ref_t owner, gc_node_ref_t ref, std::string_view lane) {
        if (ref == kInvalidNodeRef) {
            return;
        }
        if (!draft.alive(ref)) {
            const auto *ownerHeader = owner == kInvalidNodeRef ? nullptr : draft.header(owner);
            throw std::runtime_error(
                std::format(
                    "Draft payload planning found dead node ref {} in {} list of node {} "
                    "(kind={}).",
                    ref,
                    lane,
                    owner,
                    ownerHeader ? static_cast<int>(ownerHeader->kind) : -1));
        }
    };

    DraftPayloadPlan plan;
    plan.runtimeRefsByDraftId.assign(draft.nodeSlotCount(), kInvalidNodeRef);

    gc_off_t currentNodeOffset = 0;
    size_t edgeCount           = 0;
    size_t branchArmCount      = 0;
    for (gc_node_ref_t draftId = 0; draftId < draft.nodeSlotCount(); ++draftId) {
        if (!draft.alive(draftId)) {
            continue;
        }
        const DraftNodeHeader *header = draft.header(draftId);
        ASSERT(header != nullptr, "Draft payload plan requires a non-null node header.");
        for (gc_node_ref_t ref : draft.normInputsOf(draftId)) {
            requireAlive(draftId, ref, "norm-input");
        }
        for (gc_node_ref_t ref : draft.withInputsOf(draftId)) {
            requireAlive(draftId, ref, "with-input");
        }
        for (gc_node_ref_t ref : draft.ctrlInputsOf(draftId)) {
            requireAlive(draftId, ref, "ctrl-input");
        }
        for (gc_node_ref_t ref : draft.normUsersOf(draftId)) {
            requireAlive(draftId, ref, "norm-user");
        }
        for (gc_node_ref_t ref : draft.withUsersOf(draftId)) {
            requireAlive(draftId, ref, "with-user");
        }
        for (gc_node_ref_t ref : draft.ctrlUsersOf(draftId)) {
            requireAlive(draftId, ref, "ctrl-user");
        }
        if (header->kind == GCNodeKind::Brch) {
            for (const GCBranchArm &arm : draft.branchArmsOf(draftId)) {
                requireAlive(draftId, arm.head, "branch-head");
                requireAlive(draftId, arm.tail, "branch-tail");
            }
        }
        const gc_cnt_t blockCount = draftNodeBlockCount(*header);
        plan.nodes.push_back(
            PlannedDraftNode{
                .draftId    = draftId,
                .ref        = currentNodeOffset,
                .blockCount = blockCount,
            });
        plan.runtimeRefsByDraftId[draftId] = currentNodeOffset;
        currentNodeOffset                  = narrowIntegral<gc_off_t>(
            static_cast<size_t>(currentNodeOffset) + blockCount,
            "Draft runtime node blob size");
        edgeCount += draft.normInputsOf(draftId).size() + draft.withInputsOf(draftId).size() +
                     draft.ctrlInputsOf(draftId).size() + draft.normUsersOf(draftId).size() +
                     draft.withUsersOf(draftId).size() + draft.ctrlUsersOf(draftId).size();
        if (header->kind == GCNodeKind::Brch) {
            branchArmCount += draft.branchArmsOf(draftId).size();
        }
    }

    for (gc_node_ref_t ref : draft.normPorts()) {
        requireAlive(kInvalidNodeRef, ref, "norm-port");
    }
    for (gc_node_ref_t ref : draft.withPorts()) {
        requireAlive(kInvalidNodeRef, ref, "with-port");
    }
    for (gc_node_ref_t ref : draft.closureNodes()) {
        requireAlive(kInvalidNodeRef, ref, "closure");
    }
    requireAlive(kInvalidNodeRef, draft.entryNode(), "entry");
    requireAlive(kInvalidNodeRef, draft.exitNode(), "exit");
    requireAlive(kInvalidNodeRef, draft.outputNode(), "output");
    requireAlive(kInvalidNodeRef, draft.returnNode(), "return");

    plan.nodeBlockCount =
        narrowIntegral<gc_cnt_t>(currentNodeOffset, "Draft runtime node-blob block count");
    plan.edgeCount = narrowIntegral<gc_cnt_t>(edgeCount, "Draft runtime edge arena size");
    plan.branchArmCount =
        narrowIntegral<gc_cnt_t>(branchArmCount, "Draft runtime branch-arm arena size");
    return plan;
}

GCGraphPayloadShape describeDraftPayload(const GraphDraft &draft, const DraftPayloadPlan &plan) {
    return GCGraphPayloadShape{
        .nodeCount      = narrowIntegral<gc_cnt_t>(plan.nodes.size(), "Draft runtime node count"),
        .nodeBlockCount = plan.nodeBlockCount,
        .edgeCount      = plan.edgeCount,
        .normPortCount =
            narrowIntegral<gc_cnt_t>(draft.normPorts().size(), "Draft runtime norm-port count"),
        .withPortCount =
            narrowIntegral<gc_cnt_t>(draft.withPorts().size(), "Draft runtime with-port count"),
        .closureCount =
            narrowIntegral<gc_cnt_t>(draft.closureNodes().size(), "Draft runtime closure count"),
        .branchArmCount = plan.branchArmCount,
    };
}

void emitDraftPayload(
    const GraphDraft &draft, const DraftPayloadPlan &plan, GCGraphPayloadArena &payload,
    const std::function<GCGraph *(GCGraph *)> &resolveRuntimeGraph) {
    ASSERT(
        payload.nodeCount ==
            narrowIntegral<gc_cnt_t>(plan.nodes.size(), "Draft runtime node count"),
        "Draft payload arena shape does not match the planned node count.");

    gc_off_t edgeCursor      = 0;
    gc_off_t branchArmCursor = 0;
    auto appendSlice         = [&](std::span<const gc_node_ref_t> refs) -> Slice {
        Slice slice{.offset = edgeCursor, .count = 0};
        for (gc_node_ref_t draftRef : refs) {
            ASSERT(
                draftRef < plan.runtimeRefsByDraftId.size(),
                "Draft edge emission encountered a node outside the payload plan.");
            const gc_node_ref_t runtimeRef = plan.runtimeRefsByDraftId[draftRef];
            ASSERT(runtimeRef != kInvalidNodeRef, "Draft edge emission encountered a dead node.");
            payload.edges[edgeCursor++] = runtimeRef;
        }
        slice.count = narrowIntegral<gc_cnt_t>(edgeCursor - slice.offset, "Draft edge slice count");
        return slice;
    };

    for (const PlannedDraftNode &planned : plan.nodes) {
        const DraftNodeHeader *draftHeader = draft.header(planned.draftId);
        ASSERT(draftHeader != nullptr, "Draft payload emission requires a non-null node header.");
        auto *header =
            reinterpret_cast<GCNode *>(mutableNodeStorage(payload.nodeBlocks, planned.ref));
        *header = GCNode{
            .dataIndex   = draftHeader->dataIndex,
            .blockCount  = planned.blockCount,
            .normInputs  = appendSlice(draft.normInputsOf(planned.draftId)),
            .withInputs  = appendSlice(draft.withInputsOf(planned.draftId)),
            .ctrlInputs  = appendSlice(draft.ctrlInputsOf(planned.draftId)),
            .normOutputs = appendSlice(draft.normUsersOf(planned.draftId)),
            .withOutputs = appendSlice(draft.withUsersOf(planned.draftId)),
            .ctrlOutputs = appendSlice(draft.ctrlUsersOf(planned.draftId)),
            .dataType    = effectiveDraftNodeType(draft, *draftHeader),
            .kind        = draftHeader->kind,
            .flags       = draftHeader->runtimeFlags,
        };

        const auto payloadBytes = draft.payloadOf(planned.draftId);
        switch (draftHeader->kind) {
        case GCNodeKind::Func:
            if (!payloadBytes.empty()) {
                GCFuncBody body = *reinterpret_cast<const GCFuncBody *>(payloadBytes.data());
                if (resolveRuntimeGraph) {
                    body.calleeGraph = resolveRuntimeGraph(body.calleeGraph);
                }
                writeBody(payload.nodeBlocks, planned.ref, body);
            }
            break;
        case GCNodeKind::Call:
            writeBody(
                payload.nodeBlocks,
                planned.ref,
                payloadBytes.empty() ? GCCallBody{}
                                     : *reinterpret_cast<const GCCallBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Accs:
        case GCNodeKind::Oper:
            if (!payloadBytes.empty()) {
                std::memcpy(
                    mutableNodeStorage(payload.nodeBlocks, planned.ref) + sizeof(GCNode),
                    payloadBytes.data(),
                    payloadBytes.size_bytes());
            }
            break;
        case GCNodeKind::Fill:
            writeBody(
                payload.nodeBlocks,
                planned.ref,
                payloadBytes.empty() ? GCFillBody{}
                                     : *reinterpret_cast<const GCFillBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Brch: {
            const auto *draftBody = reinterpret_cast<const DraftBrchPayload *>(payloadBytes.data());
            ASSERT(draftBody != nullptr, "Draft BRCH payload is missing.");
            const gc_off_t armOffset = branchArmCursor;
            const auto arms          = draft.branchArmsOf(planned.draftId);
            for (const GCBranchArm &arm : arms) {
                payload.branchArms[branchArmCursor++] = GCBranchArm{
                    .head = plan.runtimeRefsByDraftId[arm.head],
                    .tail = plan.runtimeRefsByDraftId[arm.tail],
                };
            }
            writeBody(
                payload.nodeBlocks,
                planned.ref,
                GCBrchBody{
                    .join       = plan.runtimeRefsByDraftId[draftBody->join],
                    .armOffset  = armOffset,
                    .armCount   = draftBody->armCount,
                    .defaultArm = draftBody->defaultArm == kInvalidNodeRef
                                      ? kInvalidNodeRef
                                      : plan.runtimeRefsByDraftId[draftBody->defaultArm],
                });
        } break;
        case GCNodeKind::Join: {
            GCJoinBody body = payloadBytes.empty()
                                  ? GCJoinBody{}
                                  : *reinterpret_cast<const GCJoinBody *>(payloadBytes.data());
            body.brch       = body.brch == kInvalidNodeRef ? kInvalidNodeRef
                                                           : plan.runtimeRefsByDraftId[body.brch];
            writeBody(payload.nodeBlocks, planned.ref, body);
        } break;
        default:
            break;
        }
    }

    for (size_t i = 0; i < draft.normPorts().size(); ++i) {
        payload.normPorts[i] = plan.runtimeRefsByDraftId[draft.normPorts()[i]];
    }
    for (size_t i = 0; i < draft.withPorts().size(); ++i) {
        payload.withPorts[i] = plan.runtimeRefsByDraftId[draft.withPorts()[i]];
    }
    for (size_t i = 0; i < draft.closureNodes().size(); ++i) {
        payload.closure[i] = plan.runtimeRefsByDraftId[draft.closureNodes()[i]];
    }

    payload.entryNode  = draft.entryNode() == kInvalidNodeRef
                             ? (plan.nodes.empty() ? kInvalidNodeRef : plan.nodes.front().ref)
                             : plan.runtimeRefsByDraftId[draft.entryNode()];
    payload.exitNode   = draft.exitNode() == kInvalidNodeRef
                             ? kInvalidNodeRef
                             : plan.runtimeRefsByDraftId[draft.exitNode()];
    payload.outputNode = draft.outputNode() == kInvalidNodeRef
                             ? payload.exitNode
                             : plan.runtimeRefsByDraftId[draft.outputNode()];
    payload.returnNode = draft.returnNode() == kInvalidNodeRef
                             ? kInvalidNodeRef
                             : plan.runtimeRefsByDraftId[draft.returnNode()];
    payload.returnKind = draft.returnKind();

    ASSERT(
        edgeCursor == payload.edgeCount,
        "Draft direct payload emission did not match edge count.");
    ASSERT(
        branchArmCursor == payload.branchArmCount,
        "Draft direct payload emission did not match branch-arm count.");
}

} // namespace

void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, Type *type,
    std::unordered_set<const Object *> &visited);

GCGraphDebugRecord *
createGraphDebugRecord(std::string stableId, std::string mangledName, std::string name) {
    auto *record        = new GCGraphDebugRecord();
    record->stableId    = std::move(stableId);
    record->mangledName = std::move(mangledName);
    record->name        = std::move(name);
    return record;
}

GCGraphNativePayload
allocPayloadInArena(FixedBufferAllocator &arena, const GCGraphPayloadShape &shape) {
    GCGraphNativePayload payload{};
    payload.nodeBlocks     = arenaAllocArray<gc_block_t>(arena, shape.nodeBlockCount);
    payload.edges          = arenaAllocArray<gc_node_ref_t>(arena, shape.edgeCount);
    payload.normPorts      = arenaAllocArray<gc_node_ref_t>(arena, shape.normPortCount);
    payload.withPorts      = arenaAllocArray<gc_node_ref_t>(arena, shape.withPortCount);
    payload.closure        = arenaAllocArray<gc_node_ref_t>(arena, shape.closureCount);
    payload.branchArms     = arenaAllocArray<GCBranchArm>(arena, shape.branchArmCount);
    payload.nodeCount      = shape.nodeCount;
    payload.nodeBlockCount = shape.nodeBlockCount;
    payload.edgeCount      = shape.edgeCount;
    payload.normPortCount  = shape.normPortCount;
    payload.withPortCount  = shape.withPortCount;
    payload.closureCount   = shape.closureCount;
    payload.branchArmCount = shape.branchArmCount;
    return payload;
}

GCGraphPayloadArena makePayloadArenaView(GCGraphNativePayload &payload) {
    return GCGraphPayloadArena{
        .nodeBlocks     = payload.nodeBlocks,
        .edges          = payload.edges,
        .normPorts      = payload.normPorts,
        .withPorts      = payload.withPorts,
        .closure        = payload.closure,
        .branchArms     = payload.branchArms,
        .nodeCount      = payload.nodeCount,
        .nodeBlockCount = payload.nodeBlockCount,
        .edgeCount      = payload.edgeCount,
        .normPortCount  = payload.normPortCount,
        .withPortCount  = payload.withPortCount,
        .closureCount   = payload.closureCount,
        .branchArmCount = payload.branchArmCount,
        .entryNode      = payload.entryNode,
        .exitNode       = payload.exitNode,
        .outputNode     = payload.outputNode,
        .returnNode     = payload.returnNode,
        .returnKind     = payload.returnKind,
    };
}

void syncPayloadArenaView(GCGraphNativePayload &payload, const GCGraphPayloadArena &view) {
    payload.entryNode  = view.entryNode;
    payload.exitNode   = view.exitNode;
    payload.outputNode = view.outputNode;
    payload.returnNode = view.returnNode;
    payload.returnKind = view.returnKind;
}

::Tuple *buildStaticAreaInArena(
    FixedBufferAllocator &arena, const TupleType *staticType, std::span<const slot_t> slots) {
    if (!staticType || slots.empty()) {
        return nullptr;
    }
    ::Tuple *staticArea = ::Tuple::create(staticType->size(), arena);
    for (size_t i = 1; i < staticType->size() && i < slots.size(); ++i) {
        staticArea->set<slot_t>(i, slots[i]);
    }
    return staticArea;
}

GCGraph *GCGraphBuildAccess::create(
    GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
    camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GCGraph *outerGraph,
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payloadShape,
    const std::function<void(GCGraphPayloadArena &)> &emitPayload,
    std::span<const slot_t> staticSlots) {
    const size_t bytes =
        requiredBytes(dependencies, subGraphs, staticGraphRefs, payloadShape, staticSlots);
    void *mem = mm::graphSpace().alloc(bytes, alignof(GCGraph));
    if (!mem) {
        throw std::bad_alloc();
    }
    return constructInPlace(
        mem,
        bytes,
        debugRecord,
        funcType,
        runtimeDataType,
        staticDataType,
        closureType,
        outerGraph,
        dependencies,
        subGraphs,
        staticGraphRefs,
        payloadShape,
        emitPayload,
        staticSlots);
}

size_t GCGraphBuildAccess::requiredBytes(
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payload,
    std::span<const slot_t> staticSlots) {
    return planGraphArenaLayout(dependencies, subGraphs, staticGraphRefs, payload, staticSlots)
        .totalBytes;
}

GCGraph *GCGraphBuildAccess::constructInPlace(
    void *memory, size_t bytes, GCGraphDebugRecord *debugRecord,
    camel::core::type::FunctionType *funcType, camel::core::type::TupleType *runtimeDataType,
    camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
    GCGraph *outerGraph, std::span<GCGraph *const> dependencies,
    std::span<GCGraph *const> subGraphs, std::span<GCGraph *const> staticGraphRefs,
    const GCGraphPayloadShape &payloadShape,
    const std::function<void(GCGraphPayloadArena &)> &emitPayload,
    std::span<const slot_t> staticSlots) {
    ASSERT(memory != nullptr, "GCGraph in-place construction requires valid memory.");
    FixedBufferAllocator arena(memory, bytes);
    void *graphMem = arena.alloc(sizeof(GCGraph), alignof(GCGraph));
    ASSERT(graphMem == memory, "Graph arena must place GCGraph at the start of its owned block.");

    GCGraph **dependencyStorage       = arenaCopyArray<GCGraph *>(arena, dependencies);
    GCGraph **subGraphStorage         = arenaCopyArray<GCGraph *>(arena, subGraphs);
    GCGraph **staticRefStorage        = arenaCopyArray<GCGraph *>(arena, staticGraphRefs);
    ::Tuple *staticArea               = buildStaticAreaInArena(arena, staticDataType, staticSlots);
    GCGraphNativePayload ownedPayload = allocPayloadInArena(arena, payloadShape);
    GCGraphPayloadArena payloadArena  = makePayloadArenaView(ownedPayload);
    emitPayload(payloadArena);
    syncPayloadArenaView(ownedPayload, payloadArena);

    auto *graph = new (graphMem) GCGraph(
        debugRecord,
        funcType,
        runtimeDataType,
        staticDataType,
        closureType,
        outerGraph,
        dependencyStorage,
        subGraphStorage,
        staticRefStorage,
        narrowIntegral<gc_cnt_t>(dependencies.size(), "Runtime dependency count"),
        narrowIntegral<gc_cnt_t>(subGraphs.size(), "Runtime subgraph count"),
        narrowIntegral<gc_cnt_t>(staticGraphRefs.size(), "Runtime static-graph-ref count"),
        ownedPayload,
        staticArea);

    for (GCGraph *subGraph : subGraphs) {
        if (subGraph) {
            subGraph->outerGraph_ = graph;
        }
    }
    ASSERT(arena.available() == 0, "Graph arena planning did not match graph emission.");
    return graph;
}

GCGraph::GCGraph(
    GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
    camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GCGraph *outerGraph, GCGraph **dependencies,
    GCGraph **subGraphs, GCGraph **staticGraphRefs, gc_cnt_t dependencyCount,
    gc_cnt_t subGraphCount, gc_cnt_t staticGraphRefCount, const GCGraphNativePayload &payload,
    ::Tuple *staticArea)
    : debug_(debugRecord), funcType_(funcType), runtimeDataType_(runtimeDataType),
      staticDataType_(staticDataType), closureType_(closureType), outerGraph_(outerGraph),
      dependencies_(dependencies), subGraphs_(subGraphs), staticGraphRefs_(staticGraphRefs),
      dependencyCount_(dependencyCount), subGraphCount_(subGraphCount),
      staticGraphRefCount_(staticGraphRefCount), nativePayload_(payload), staticArea_(staticArea) {}

GCGraphPayloadShape describeDraftNativePayload(const GraphDraft &draft) {
    return describeDraftPayload(draft, planDraftPayload(draft));
}

void emitDraftNativePayload(
    const GraphDraft &draft, GCGraphPayloadArena &payload,
    const std::function<GCGraph *(GCGraph *)> &resolveRuntimeGraph) {
    emitDraftPayload(draft, planDraftPayload(draft), payload, resolveRuntimeGraph);
}

void validateRuntimeGraphPayload(const GCGraph *graph) { validateRuntimeGraphPayloadImpl(graph); }

const std::string &GCGraph::stableId() const {
    static const std::string kEmpty;
    return debug_ ? debug_->stableId : kEmpty;
}

const std::string &GCGraph::mangledName() const {
    static const std::string kEmpty;
    return debug_ ? debug_->mangledName : kEmpty;
}

const std::string &GCGraph::name() const {
    static const std::string kEmpty;
    return debug_ ? debug_->name : kEmpty;
}

camel::core::type::FunctionType *GCGraph::funcType() const { return funcType_; }

const TupleType *GCGraph::runtimeDataType() const { return runtimeDataType_; }

const TupleType *GCGraph::staticDataType() const { return staticDataType_; }

const TupleType *GCGraph::closureType() const { return closureType_; }

bool GCGraph::hasFrameLayout() const {
    return staticArea_ != nullptr && runtimeDataType() != nullptr;
}

size_t GCGraph::frameSize() const {
    ASSERT(hasFrameLayout(), "Runtime graph frame layout is not available.");
    return sizeof(camel::core::context::Frame) + sizeof(slot_t) * runtimeDataType()->size();
}

bool GCGraph::isMacro() const {
    auto *type = funcType();
    return type != nullptr && type->modifiers().macro();
}

GCGraph *GCGraph::outerGraph() const { return outerGraph_; }

bool GCGraph::isRoot() const { return outerGraph_ == nullptr; }

uintptr_t GCGraph::extraSlot(size_t index) const {
    ASSERT(index < kExtraSlotCount, "GCGraph extra slot index out of range.");
    return extraSlots_[index];
}

void GCGraph::setExtraSlot(size_t index, uintptr_t value) {
    ASSERT(index < kExtraSlotCount, "GCGraph extra slot index out of range.");
    extraSlots_[index] = value;
}

void GCGraph::clearExtraSlots() {
    for (uintptr_t &slot : extraSlots_) {
        slot = 0;
    }
}

std::span<GCGraph *const> GCGraph::dependencies() const {
    return {dependencies_, dependencyCount_};
}

std::span<GCGraph *const> GCGraph::subGraphs() const { return {subGraphs_, subGraphCount_}; }

std::span<GCGraph *const> GCGraph::staticGraphRefs() const {
    return {staticGraphRefs_, staticGraphRefCount_};
}

std::span<const slot_t> GCGraph::staticSlots() const {
    return staticArea_ ? std::span<const slot_t>(staticArea_->data(), staticArea_->size())
                       : std::span<const slot_t>{};
}

bool GCGraph::hasNodePayload() const { return !nativePayload_.empty(); }

const GCGraphNativePayload *GCGraph::nodePayload() const {
    return nativePayload_.empty() ? nullptr : &nativePayload_;
}

gc_cnt_t GCGraph::nodeCount() const { return nativePayload_.nodeCount; }

gc_cnt_t GCGraph::nodeBlockCount() const { return nativePayload_.nodeBlockCount; }

gc_cnt_t GCGraph::edgeCount() const { return nativePayload_.edgeCount; }

gc_cnt_t GCGraph::branchArmCount() const { return nativePayload_.branchArmCount; }

const GCNode *GCGraph::entryNode() const { return node(entryNodeRef()); }

gc_node_ref_t GCGraph::entryNodeRef() const { return nativePayload_.entryNode; }

const GCNode *GCGraph::exitNode() const { return node(exitNodeRef()); }

gc_node_ref_t GCGraph::exitNodeRef() const { return nativePayload_.exitNode; }

const GCNode *GCGraph::outputNode() const { return node(outputNodeRef()); }

gc_node_ref_t GCGraph::outputNodeRef() const { return nativePayload_.outputNode; }

const GCNode *GCGraph::returnNode() const { return node(returnNodeRef()); }

gc_node_ref_t GCGraph::returnNodeRef() const { return nativePayload_.returnNode; }

GCReturnKind GCGraph::returnKind() const { return nativePayload_.returnKind; }

const GCNode *GCGraph::node(gc_node_ref_t ref) const {
    const auto &payload = nativePayload_;
    if (!payload.nodeBlocks || ref == kInvalidNodeRef || ref >= payload.nodeBlockCount) {
        return nullptr;
    }
    return reinterpret_cast<const GCNode *>(nodeStorage(payload.nodeBlocks, ref));
}

bool GCGraph::containsNodeRef(gc_node_ref_t ref) const { return node(ref) != nullptr; }

gc_node_ref_t GCGraph::nodeRef(const GCNode *nodePtr) const {
    const auto &payload = nativePayload_;
    if (!payload.nodeBlocks || !nodePtr) {
        return kInvalidNodeRef;
    }

    const auto *base      = reinterpret_cast<const std::byte *>(payload.nodeBlocks);
    const auto *nodeBytes = reinterpret_cast<const std::byte *>(nodePtr);
    const auto *limit     = base + static_cast<size_t>(payload.nodeBlockCount) * kGCBlockBytes;
    if (nodeBytes < base || nodeBytes >= limit) {
        return kInvalidNodeRef;
    }

    const size_t byteOffset = static_cast<size_t>(nodeBytes - base);
    if ((byteOffset % kGCBlockBytes) != 0) {
        return kInvalidNodeRef;
    }
    return narrowIntegral<gc_node_ref_t>(byteOffset / kGCBlockBytes, "Runtime node reference");
}

GCGraph *GCGraph::directCalleeGraphOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Func || !n->hasBody()) {
        return nullptr;
    }
    return n->bodyAs<GCFuncBody>()->calleeGraph;
}

gc_node_ref_t GCGraph::matchedJoinOutputOf(gc_node_ref_t ref) const {
    const auto outputs = withOutputsOf(ref);
    if (outputs.size() != 1) {
        return kInvalidNodeRef;
    }
    const gc_node_ref_t candidate = outputs.front();
    const GCNode *joinNode        = node(candidate);
    if (!joinNode || joinNode->kind != GCNodeKind::Join) {
        return kInvalidNodeRef;
    }
    return candidate;
}

gc_node_ref_t GCGraph::nextNodeRef(gc_node_ref_t ref) const {
    const GCNode *current = node(ref);
    if (!current) {
        return nodeBlockCount();
    }
    return narrowIntegral<gc_node_ref_t>(
        static_cast<size_t>(ref) + current->blockCount,
        "Runtime next-node reference");
}

const GCNode *GCGraph::nextNode(gc_node_ref_t ref) const { return node(nextNodeRef(ref)); }

std::span<const gc_node_ref_t> GCGraph::edgeSlice(Slice slice) const {
    const auto &payload = nativePayload_;
    return sliceView(payload.edges, slice, payload.edgeCount);
}

std::span<const gc_node_ref_t> GCGraph::normInputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->normInputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::withInputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->withInputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::ctrlInputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->ctrlInputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::normOutputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->normOutputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::withOutputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->withOutputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::ctrlOutputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->ctrlOutputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::normPorts() const {
    const auto &payload = nativePayload_;
    if (!payload.normPorts) {
        return {};
    }
    return {payload.normPorts, payload.normPortCount};
}

std::span<const gc_node_ref_t> GCGraph::withPorts() const {
    const auto &payload = nativePayload_;
    if (!payload.withPorts) {
        return {};
    }
    return {payload.withPorts, payload.withPortCount};
}

std::span<const gc_node_ref_t> GCGraph::closureNodes() const {
    const auto &payload = nativePayload_;
    if (!payload.closure) {
        return {};
    }
    return {payload.closure, payload.closureCount};
}

std::span<const GCBranchArm> GCGraph::branchArmsOf(gc_node_ref_t brchRef) const {
    const auto &payload = nativePayload_;
    const GCNode *n     = node(brchRef);
    if (!n || n->kind != GCNodeKind::Brch) {
        return {};
    }
    const auto *body = n->bodyAs<GCBrchBody>();
    return branchSliceView(
        payload.branchArms,
        body->armOffset,
        body->armCount,
        payload.branchArmCount);
}

bool GCGraph::equals(
    const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
    bool deep) const {
    (void)type;
    (void)deep;
    auto *otherGraph = dynamic_cast<const GCGraph *>(other);
    return otherGraph != nullptr &&
           otherGraph->nativePayload_.nodeBlocks == nativePayload_.nodeBlocks &&
           otherGraph->funcType_ == funcType_;
}

camel::core::rtdata::Object *GCGraph::clone(
    camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type, bool deep) const {
    (void)type;
    (void)deep;
    void *mem = allocator.alloc(sizeof(GCGraph), alignof(GCGraph));
    if (!mem) {
        throw std::bad_alloc();
    }
    auto *graph = new (mem) GCGraph(
        debug_,
        funcType_,
        runtimeDataType_,
        staticDataType_,
        closureType_,
        outerGraph_,
        dependencies_,
        subGraphs_,
        staticGraphRefs_,
        dependencyCount_,
        subGraphCount_,
        staticGraphRefCount_,
        nativePayload_,
        staticArea_);
    for (size_t i = 0; i < kExtraSlotCount; ++i) {
        graph->extraSlots_[i] = extraSlots_[i];
    }
    return graph;
}

void GCGraph::print(std::ostream &os, const camel::core::type::Type *type) const {
    (void)type;
    os << "GCGraph(name=" << name() << ", nodeBlocks=" << nodeBlockCount() << ")";
}

void GCGraph::updateRefs(
    const std::function<camel::core::rtdata::Object *(camel::core::rtdata::Object *)> &relocate,
    const camel::core::type::Type *type) {
    (void)type;
    if (staticArea_ && staticDataType()) {
        staticArea_->updateRefs(relocate, staticDataType());
    }
}

void GCGraphManager::replaceRoot(GCGraph *rootGraph) {
    clear();
    adoptRoot(rootGraph);
}

void GCGraphManager::adoptRoot(GCGraph *rootGraph) {
    root_ = rootGraph;
    if (!root_) {
        return;
    }

    graphs_ = collectReachableGraphs(root_);
    gcRoots_.reserve(graphs_.size());
    for (GCGraph *graph : graphs_) {
        if (!graph) {
            continue;
        }
        gcRoots_.push_back(graph);
        if (graph->debug_) {
            debugRecords_.push_back(graph->debug_);
        }
    }
}

GCGraphManager::~GCGraphManager() { clear(); }

std::vector<GCGraph *> GCGraphManager::roots() const {
    if (!root_) {
        return {};
    }
    return {root_};
}

std::vector<GCGraph *> GCGraphManager::reachableFromRoots() const {
    return collectReachableGraphs(root_);
}

void GCGraphManager::clear() {
    gcRoots_.clear();
    debugRecords_.clear();
    graphs_.clear();
    root_ = nullptr;
}

void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, Type *type,
    std::unordered_set<const Object *> &visited) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return;
    }

    Object *object = fromSlot<Object *>(slot);
    if (!object || !visited.insert(object).second) {
        return;
    }

    switch (type->code()) {
    case TypeCode::Function: {
        auto *funcObj = fromSlot<::Function *>(slot);
        if (!funcObj) {
            return;
        }
        GCGraph *runtimeGraph = funcObj->runtimeGraph();
        ASSERT(
            runtimeGraph != nullptr,
            "Runtime static graph references must already point to GCGraph carriers.");
        pushUnique(refs, runtimeGraph);

        if (::Tuple *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                    continue;
                }
                collectStaticGraphRefsRecursive(
                    refs,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i),
                    visited);
            }
        }
    } break;
    case TypeCode::Tuple: {
        auto *tuple     = fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<TupleType *>(type);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                continue;
            }
            collectStaticGraphRefsRecursive(
                refs,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
                visited);
        }
    } break;
    case TypeCode::Array: {
        auto *array     = fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<ArrayType *>(type);
        if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
            return;
        }
        for (size_t i = 0; i < array->size(); ++i) {
            collectStaticGraphRefsRecursive(
                refs,
                array->get<slot_t>(i),
                arrayType->elemType(),
                visited);
        }
    } break;
    case TypeCode::Struct: {
        auto *st         = fromSlot<::Struct *>(slot);
        auto *structType = static_cast<StructType *>(type);
        for (size_t i = 0; i < structType->size(); ++i) {
            if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
                continue;
            }
            collectStaticGraphRefsRecursive(
                refs,
                st->get<slot_t>(i),
                structType->typeAt(i),
                visited);
        }
    } break;
    default:
        break;
    }
}

} // namespace camel::runtime
