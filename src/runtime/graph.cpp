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
 * Updated: Oct. 19, 2026
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
#include "camel/runtime/capacity.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/loop.h"
#include "runtime/graph_build.h"

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/composite.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
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
        if (const auto slots = draft.runtimeSlotTypes();
            static_cast<size_t>(header.dataIndex) < slots.size()) {
            return slots[static_cast<size_t>(header.dataIndex)];
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

/// The TypeId a node record stores for `type`: types outside the type table (open signatures,
/// unions, type variables) are given one here (TypeTable::adopt), so every type a frozen graph
/// names has an id.
camel::core::type::TypeId typeIdOf(Type *type) {
    if (type == nullptr) {
        return camel::core::type::kNoTypeId;
    }
    if (type->typeId() == camel::core::type::kNoTypeId) {
        camel::core::type::TypeTable::instance().adopt(type);
    }
    return type->typeId();
}

struct GraphArenaLayout {
    size_t graphBytes       = 0;
    size_t refBytes         = 0; // the reference table
    size_t refRoleBytes     = 0; // its roles, padded to the next array's alignment
    size_t tupleBytes       = 0;
    size_t nodeBytes        = 0;
    size_t originBytes      = 0;
    size_t permissionBytes  = 0;
    size_t attrBytes        = 0;
    size_t payloadBytes     = 0;
    size_t inputBytes       = 0;
    size_t normPortBytes    = 0;
    size_t withPortBytes    = 0;
    size_t closureBytes     = 0;
    size_t branchArmBytes   = 0;
    size_t frameLayoutBytes = 0;
    size_t totalBytes       = 0;
};

GraphArenaLayout planGraphArenaLayout(
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payload,
    std::span<const slot_t> staticSlots, const FrameSlotTypes &frameSlots) {
    GraphArenaLayout layout{
        .graphBytes = camel::core::mm::alignUp(sizeof(GCGraph), alignof(GCGraph)),
        // Room for every graph the three lists name; a graph named twice takes one entry and
        // leaves its room unused.
        .refBytes = bytesForArray<GCGraph *>(
            dependencies.size() + subGraphs.size() + staticGraphRefs.size()),
        .refRoleBytes = camel::core::mm::alignUp(
            dependencies.size() + subGraphs.size() + staticGraphRefs.size(),
            alignof(GCGraph *)),
        .tupleBytes = staticSlots.empty()
                          ? 0
                          : camel::core::mm::alignUp(
                                sizeof(::Tuple) + sizeof(slot_t) * staticSlots.size(),
                                alignof(::Tuple)),
        // nodeCount records and the sentinel; a multiple of 16 bytes.
        .nodeBytes = sizeof(GCNode) * (static_cast<size_t>(payload.nodeCount) + 1),
        // The origin column, padded to the payload words that follow it.
        .originBytes = camel::core::mm::alignUp(
            sizeof(uint32_t) * static_cast<size_t>(payload.nodeCount),
            alignof(gc_payload_word_t)),
        // The permission column, only when some node has permissions.
        .permissionBytes = payload.permissions ? camel::core::mm::alignUp(
                                                     static_cast<size_t>(payload.nodeCount),
                                                     alignof(gc_payload_word_t))
                                               : 0,
        // The attribute column, only when some node has attributes.
        .attrBytes    = payload.attrs ? camel::core::mm::alignUp(
                                            sizeof(uint32_t) * static_cast<size_t>(payload.nodeCount),
                                            alignof(gc_payload_word_t))
                                      : 0,
        .payloadBytes = bytesForArray<gc_payload_word_t>(payload.payloadWordCount),
        .inputBytes   = camel::core::mm::alignUp(
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
    const size_t beforeFrameLayout =
        layout.graphBytes + layout.refBytes + layout.refRoleBytes + layout.tupleBytes +
        layout.nodeBytes + layout.originBytes + layout.permissionBytes + layout.attrBytes +
        layout.payloadBytes + layout.inputBytes + layout.normPortBytes + layout.withPortBytes +
        layout.closureBytes + layout.branchArmBytes;
    // The frame layout comes last. The arrays before it may end at any 2-byte boundary, so the
    // padding up to the layout's alignment is part of its share.
    if (frameSlots) {
        layout.frameLayoutBytes = camel::core::mm::alignUp(beforeFrameLayout, alignof(TupleType)) -
                                  beforeFrameLayout + TupleType::frameLayoutBytes(*frameSlots);
    }
    layout.totalBytes = beforeFrameLayout + layout.frameLayoutBytes;
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

/// `value` as the 16-bit quantity of a runtime graph it is stored as. A graph that needs more
/// of `what` (a plural noun phrase) than that is too large for the representation, which is
/// reported as such (see camel/runtime/capacity.h); the caller that knows the graph names it.
template <typename T, typename U> T narrowIntegral(U value, const char *what) {
    if (value > static_cast<U>(std::numeric_limits<T>::max())) {
        throw GraphCapacityError(
            what,
            static_cast<size_t>(value),
            static_cast<size_t>(std::numeric_limits<T>::max()));
    }
    return static_cast<T>(value);
}

bool shouldTraverseStaticSlotType(Type *type) {
    if (type == nullptr || !type->isGCTraced() || type->code() == TypeCode::Ref) {
        return false;
    }
    if (type->isComposite() && !static_cast<camel::core::type::CompositeType *>(type)->resolved()) {
        return false;
    }
    return true;
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
    for (gc_node_ref_t ref : graph->normPorts()) {
        validateRef(ref, "norm-port");
    }
    for (gc_node_ref_t ref : graph->withPorts()) {
        validateRef(ref, "with-port");
    }
    for (gc_node_ref_t ref : graph->closureNodes()) {
        validateRef(ref, "closure-node");
    }
    const size_t runtimeSlotCount = graph->runtimeDataType() ? graph->runtimeDataType()->size() : 0;
    const size_t staticSlotCount  = graph->staticSlots().size();

    const GCGraphNativePayload *payload = graph->nodePayload();
    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
        const GCNode *node = graph->node(ref);
        // The input runs are consecutive and inside the input table; the sentinel after the
        // last node closes the last run.
        const GCNode *next = node + 1;
        if (!(node->inBegin <= node->withBegin && node->withBegin <= node->ctrlBegin &&
              node->ctrlBegin <= next->inBegin && next->inBegin <= payload->edgeCount)) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has node {} with inputs outside the input table.",
                    graph->name(),
                    ref));
        }
        if (node->hasPayload() && node->payload >= payload->payloadWordCount) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has node {} with a payload outside the payload area.",
                    graph->name(),
                    ref));
        }
        switch (node->kind) {
        case GCNodeKind::Func:
        case GCNodeKind::Call:
        case GCNodeKind::Accs:
        case GCNodeKind::Fill:
        case GCNodeKind::Brch:
        case GCNodeKind::Join:
        case GCNodeKind::Oper:
        case GCNodeKind::Loop:
            if (!node->hasPayload()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has node {} of a kind with a payload without one.",
                        graph->name(),
                        ref));
            }
            break;
        default:
            break;
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
        if (node->kind == GCNodeKind::Dref) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' contains unresolved DREF node at ref {}.",
                    graph->name(),
                    ref));
        }
        for (gc_node_ref_t input : graph->normInputsOf(ref)) {
            validateRef(input, "norm-input");
        }
        for (gc_node_ref_t input : graph->withInputsOf(ref)) {
            validateRef(input, "with-input");
        }
        for (gc_node_ref_t input : graph->ctrlInputsOf(ref)) {
            validateRef(input, "ctrl-input");
        }
        if (node->kind == GCNodeKind::Func) {
            // A graph names another graph only through its reference table (I12/I13).
            const auto *body = graph->nodeBodyAs<GCFuncRef>(ref);
            if (!body || body->ref >= graph->refs().size() || !graph->refs()[body->ref] ||
                (graph->refRoles(body->ref) & kGraphRefCallee) == 0) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has FUNC node {} without callee graph.",
                        graph->name(),
                        ref));
            }
        }
        if (node->kind == GCNodeKind::Loop) {
            // The two graphs are entries of the reference table, marked callee (I12/I13): the
            // condition exactly when the form has one.
            const auto *body     = graph->nodeBodyAs<GCLoopRef>(ref);
            const auto validLoop = [&](uint32_t index) {
                return index < graph->refs().size() && graph->refs()[index] &&
                       (graph->refRoles(index) & kGraphRefCallee) != 0;
            };
            const bool hasCond = camel::core::loopHasCondition(body->kind);
            if (static_cast<uint8_t>(body->kind) >
                    static_cast<uint8_t>(camel::core::LoopKind::ForWhile) ||
                !validLoop(body->bodyRef) ||
                (hasCond ? !validLoop(body->condRef) : body->condRef != kNoLoopGraph)) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has LOOP node {} without its condition or body graph.",
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
            if (graph->normInputsOf(ref).empty()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has GATE node {} without value input.",
                        graph->name(),
                        ref));
            }
        }
        if (node->kind == GCNodeKind::Brch) {
            const auto *body = graph->nodeBodyAs<GCBrchBody>(ref);
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
            const auto *body = graph->nodeBodyAs<GCJoinBody>(ref);
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
    }
}

template <typename Body>
void writeBody(gc_payload_word_t *payload, const GCNode &node, const Body &body) {
    static_assert(
        std::is_trivially_copyable_v<Body>,
        "GC node bodies must remain trivially copyable.");
    std::memcpy(payload + node.payload, &body, sizeof(Body));
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
    gc_node_ref_t draftId   = kInvalidNodeRef;
    gc_node_ref_t ref       = kInvalidNodeRef;
    gc_off_t payload        = kNoPayload; // word offset in the payload area
    size_t payloadWordCount = 0;
};

struct DraftPayloadPlan {
    std::vector<PlannedDraftNode> nodes;
    std::vector<gc_node_ref_t> runtimeRefsByDraftId;
    gc_cnt_t edgeCount        = 0;
    gc_cnt_t payloadWordCount = 0;
    gc_cnt_t branchArmCount   = 0;
};

/// Words of the payload area a node of this kind takes: its body (for ACCS, OPER and FILL the
/// bytes the draft holds), rounded up to whole words; 0 for kinds without a payload.
size_t draftPayloadWords(const DraftNodeHeader &header) {
    size_t bodyBytes = 0;
    switch (header.kind) {
    case GCNodeKind::Func:
        bodyBytes = sizeof(GCFuncRef);
        break;
    case GCNodeKind::Call:
        bodyBytes = sizeof(GCCallBody);
        break;
    case GCNodeKind::Accs:
    case GCNodeKind::Oper:
    case GCNodeKind::Fill:
        bodyBytes = std::max<size_t>(header.payloadBytes, 1);
        break;
    case GCNodeKind::Brch:
        bodyBytes = sizeof(GCBrchBody);
        break;
    case GCNodeKind::Join:
        bodyBytes = sizeof(GCJoinBody);
        break;
    case GCNodeKind::Loop:
        bodyBytes = sizeof(GCLoopRef);
        break;
    default:
        return 0;
    }
    return (bodyBytes + kPayloadWordBytes - 1) / kPayloadWordBytes;
}

/// The capacities of a runtime graph, as the quantities a GraphCapacityError names. The node
/// and input counts are 16-bit, and so is the word offset of a payload.
constexpr const char *kNodes = "nodes";
constexpr const char *kEdges = "edges (every input of every node is one)";
constexpr const char *kPayloadWords =
    "payload words (the 8-byte units of what nodes carry beyond their record: callees, "
    "operators, constructed positions, branch tables)";

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

    // Live nodes keep their draft order and take dense NodeIds. A graph that does not fit is
    // measured to the end, so that the error states what it needs and not where counting
    // stopped.
    size_t nodeCount      = 0;
    size_t edgeCount      = 0;
    size_t payloadWords   = 0;
    size_t branchArmCount = 0;
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
        if (header->kind == GCNodeKind::Brch) {
            for (const GCBranchArm &arm : draft.branchArmsOf(draftId)) {
                requireAlive(draftId, arm.head, "branch-head");
                requireAlive(draftId, arm.tail, "branch-tail");
            }
            branchArmCount += draft.branchArmsOf(draftId).size();
        }
        const size_t words = draftPayloadWords(*header);
        // The NodeId kInvalidNodeRef and the payload offset kNoPayload are reserved.
        if (nodeCount < kInvalidNodeRef && payloadWords + words < kNoPayload) {
            plan.nodes.push_back(
                PlannedDraftNode{
                    .draftId = draftId,
                    .ref     = static_cast<gc_node_ref_t>(nodeCount),
                    .payload = words == 0 ? kNoPayload : static_cast<gc_off_t>(payloadWords),
                    .payloadWordCount = words,
                });
            plan.runtimeRefsByDraftId[draftId] = static_cast<gc_node_ref_t>(nodeCount);
        }
        ++nodeCount;
        payloadWords += words;
        edgeCount += draft.normInputsOf(draftId).size() + draft.withInputsOf(draftId).size() +
                     draft.ctrlInputsOf(draftId).size();
    }
    // One NodeId (kInvalidNodeRef) and one payload offset (kNoPayload) are reserved.
    if (nodeCount >= kInvalidNodeRef) {
        throw GraphCapacityError(kNodes, nodeCount, kInvalidNodeRef - 1);
    }
    if (payloadWords >= kNoPayload) {
        throw GraphCapacityError(kPayloadWords, payloadWords, kNoPayload - 1);
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

    plan.edgeCount        = narrowIntegral<gc_cnt_t>(edgeCount, kEdges);
    plan.payloadWordCount = static_cast<gc_cnt_t>(payloadWords);
    plan.branchArmCount   = narrowIntegral<gc_cnt_t>(branchArmCount, "branch arms");
    return plan;
}

GCGraphPayloadShape describeDraftPayload(const GraphDraft &draft, const DraftPayloadPlan &plan) {
    return GCGraphPayloadShape{
        .nodeCount        = narrowIntegral<gc_cnt_t>(plan.nodes.size(), kNodes),
        .edgeCount        = plan.edgeCount,
        .payloadWordCount = plan.payloadWordCount,
        .normPortCount    = narrowIntegral<gc_cnt_t>(draft.normPorts().size(), "parameters"),
        .withPortCount    = narrowIntegral<gc_cnt_t>(draft.withPorts().size(), "'with' parameters"),
        .closureCount   = narrowIntegral<gc_cnt_t>(draft.closureNodes().size(), "captured values"),
        .branchArmCount = plan.branchArmCount,
        .permissions    = std::any_of(
            plan.nodes.begin(),
            plan.nodes.end(),
            [&](const auto &planned) { return draft.nodePermissions(planned.draftId) != 0; }),
        .attrs = std::any_of(
            plan.nodes.begin(),
            plan.nodes.end(),
            [&](const auto &planned) { return draft.nodeAttrs(planned.draftId) != 0; }),
    };
}

void emitDraftPayload(
    const GraphDraft &draft, const DraftPayloadPlan &plan, GCGraphPayloadArena &payload,
    const std::function<GCGraph *(GCGraph *)> &resolveRuntimeGraph) {
    ASSERT(
        payload.nodeCount == narrowIntegral<gc_cnt_t>(plan.nodes.size(), "nodes"),
        "Draft payload arena shape does not match the planned node count.");

    size_t inputCursor       = 0;
    gc_off_t branchArmCursor = 0;
    // Appends the runtime refs of `refs` to the input table; returns where they begin.
    auto appendInputs = [&](std::span<const gc_node_ref_t> refs) -> gc_off_t {
        const auto begin = static_cast<gc_off_t>(inputCursor);
        for (gc_node_ref_t draftRef : refs) {
            ASSERT(
                draftRef < plan.runtimeRefsByDraftId.size(),
                "Draft edge emission encountered a node outside the payload plan.");
            const gc_node_ref_t runtimeRef = plan.runtimeRefsByDraftId[draftRef];
            ASSERT(runtimeRef != kInvalidNodeRef, "Draft edge emission encountered a dead node.");
            payload.inputs[inputCursor++] = runtimeRef;
        }
        return begin;
    };

    // A GATE forwards its (last) norm input: FastVM reads the producer, NodeVM reads the gate's
    // slot, so the slot must be the input's. Rewrites that rebind a gate's input (inlining binds
    // parameter gates to the caller's arguments) leave a stale slot, so it is derived here.
    const auto slotOf = [&](gc_node_ref_t id) {
        for (size_t hops = 0; hops < plan.nodes.size() + 1; ++hops) {
            const DraftNodeHeader *h = draft.header(id);
            if (!h || h->kind != GCNodeKind::Gate || draft.normInputsOf(id).empty()) {
                return h ? h->dataIndex : static_cast<gc_slot_idx_t>(0);
            }
            id = draft.normInputsOf(id).back();
        }
        return static_cast<gc_slot_idx_t>(0);
    };

    for (const PlannedDraftNode &planned : plan.nodes) {
        const DraftNodeHeader *draftHeader = draft.header(planned.draftId);
        ASSERT(draftHeader != nullptr, "Draft payload emission requires a non-null node header.");
        CAMEL_LOG_INFO_S(
            "GCGraphBuild",
            "Emit node draftId={} runtimeRef={} kind={} payloadBytes={}.",
            planned.draftId,
            planned.ref,
            static_cast<int>(draftHeader->kind),
            draft.payloadOf(planned.draftId).size_bytes());
        GCNode &record   = payload.nodes[planned.ref];
        record.kind      = draftHeader->kind;
        record.flags     = draftHeader->runtimeFlags;
        record.dataIndex = slotOf(planned.draftId);
        record.typeId    = typeIdOf(effectiveDraftNodeType(draft, *draftHeader));
        record.inBegin   = appendInputs(draft.normInputsOf(planned.draftId));
        record.withBegin = appendInputs(draft.withInputsOf(planned.draftId));
        record.ctrlBegin = appendInputs(draft.ctrlInputsOf(planned.draftId));
        record.payload   = planned.payload;
        // Origin ids index the origin table of the source manager, a vector of records: they
        // fit 32 bits long before memory runs out (design 3.8: a 32-bit OriginId per node).
        const uint64_t origin = draft.nodeOrigin(planned.draftId);
        ASSERT(origin <= std::numeric_limits<uint32_t>::max(), "Origin id beyond 32 bits.");
        payload.origins[planned.ref] = static_cast<uint32_t>(origin);
        if (payload.permissions) {
            payload.permissions[planned.ref] = draft.nodePermissions(planned.draftId);
        }
        if (payload.attrs) {
            payload.attrs[planned.ref] = draft.nodeAttrs(planned.draftId);
        }
        const GCNode &header = record;

        const auto payloadBytes = draft.payloadOf(planned.draftId);
        switch (draftHeader->kind) {
        case GCNodeKind::Func:
            if (!payloadBytes.empty()) {
                GCFuncBody body = *reinterpret_cast<const GCFuncBody *>(payloadBytes.data());
                CAMEL_LOG_INFO_S(
                    "GCGraphBuild",
                    "FUNC node runtimeRef={} placeholder before resolve={}.",
                    planned.ref,
                    static_cast<const void *>(body.calleeGraph));
                if (resolveRuntimeGraph) {
                    body.calleeGraph = resolveRuntimeGraph(body.calleeGraph);
                }
                CAMEL_LOG_INFO_S(
                    "GCGraphBuild",
                    "FUNC node runtimeRef={} callee after resolve={}.",
                    planned.ref,
                    static_cast<const void *>(body.calleeGraph));
                // The frozen node names the callee by its index in the reference table; a
                // callee that is not in it is a graph the encoder was not told this one calls.
                const auto *refIndex = payload.refIndex;
                const auto found =
                    refIndex ? refIndex->find(body.calleeGraph)
                             : std::unordered_map<const GCGraph *, uint32_t>::const_iterator{};
                if (refIndex == nullptr || found == refIndex->end()) {
                    throw std::runtime_error(
                        std::format(
                            "FUNC node {} calls a graph that is not among the graphs this graph "
                            "calls (its reference table).",
                            planned.ref));
                }
                writeBody(payload.payload, header, GCFuncRef{.ref = found->second});
            }
            break;
        case GCNodeKind::Loop: {
            // The draft holds the two graphs; the frozen node names them by their indexes in
            // the reference table, as a FUNC node names its callee.
            ASSERT(
                payloadBytes.size_bytes() >= sizeof(GCLoopBody),
                "Draft LOOP payload is missing.");
            GCLoopBody body = *reinterpret_cast<const GCLoopBody *>(payloadBytes.data());
            if (resolveRuntimeGraph) {
                body.condGraph = body.condGraph ? resolveRuntimeGraph(body.condGraph) : nullptr;
                body.bodyGraph = resolveRuntimeGraph(body.bodyGraph);
            }
            const auto *refIndex = payload.refIndex;
            const auto indexOf   = [&](GCGraph *target) -> uint32_t {
                if (target == nullptr) {
                    return kNoLoopGraph;
                }
                const auto found =
                    refIndex ? refIndex->find(target)
                             : std::unordered_map<const GCGraph *, uint32_t>::const_iterator{};
                if (refIndex == nullptr || found == refIndex->end()) {
                    throw std::runtime_error(
                        std::format(
                            "LOOP node {} names a graph that is not among the graphs this graph "
                            "calls (its reference table).",
                            planned.ref));
                }
                return found->second;
            };
            writeBody(
                payload.payload,
                header,
                GCLoopRef{
                    .kind            = body.kind,
                    .condRef         = indexOf(body.condGraph),
                    .bodyRef         = indexOf(body.bodyGraph),
                    .collectionCount = body.collectionCount,
                });
        } break;
        case GCNodeKind::Call:
            writeBody(
                payload.payload,
                header,
                payloadBytes.empty() ? GCCallBody{}
                                     : *reinterpret_cast<const GCCallBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Accs:
        case GCNodeKind::Oper:
        case GCNodeKind::Fill:
            if (!payloadBytes.empty()) {
                std::memcpy(
                    payload.payload + header.payload,
                    payloadBytes.data(),
                    payloadBytes.size_bytes());
            }
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
                payload.payload,
                header,
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
            writeBody(payload.payload, header, body);
        } break;
        default:
            break;
        }
    }

    // The sentinel record: its first input ends the last node's input run.
    payload.nodes[plan.nodes.size()] = GCNode{
        .inBegin   = static_cast<gc_off_t>(inputCursor),
        .withBegin = static_cast<gc_off_t>(inputCursor),
        .ctrlBegin = static_cast<gc_off_t>(inputCursor),
    };

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

    for (const PlannedDraftNode &planned : plan.nodes) {
        const DraftNodeHeader *draftHeader = draft.header(planned.draftId);
        if (draftHeader == nullptr || draftHeader->kind != GCNodeKind::Join) {
            continue;
        }
        const auto payloadBytes = draft.payloadOf(planned.draftId);
        if (payloadBytes.size_bytes() < sizeof(GCJoinBody)) {
            throw std::runtime_error(
                "Draft JOIN payload became truncated during runtime payload verification.");
        }
        const auto *expected = reinterpret_cast<const GCJoinBody *>(payloadBytes.data());
        const auto *written  = reinterpret_cast<const GCJoinBody *>(
            payload.payload + payload.nodes[planned.ref].payload);
        if (expected->armCount != written->armCount) {
            throw std::runtime_error(
                std::format(
                    "JOIN payload corruption after emit: draftId={} runtimeRef={} "
                    "expectedArmCount={} writtenArmCount={}.",
                    planned.draftId,
                    planned.ref,
                    expected->armCount,
                    written->armCount));
        }
    }

    ASSERT(
        inputCursor == payload.edgeCount,
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

void recordDraftDebugData(GCGraphDebugRecord *record, const GraphDraft &draft) {
    if (!record) {
        return;
    }
    record->captureNames.assign(draft.captureNames().begin(), draft.captureNames().end());
}

GCGraphNativePayload
allocPayloadInArena(FixedBufferAllocator &arena, const GCGraphPayloadShape &shape) {
    GCGraphNativePayload payload{};
    payload.nodes   = static_cast<GCNode *>(arena.alloc(
        sizeof(GCNode) * (static_cast<size_t>(shape.nodeCount) + 1),
        alignof(gc_payload_word_t)));
    payload.origins = static_cast<uint32_t *>(arena.alloc(
        camel::core::mm::alignUp(
            sizeof(uint32_t) * static_cast<size_t>(shape.nodeCount),
            alignof(gc_payload_word_t)),
        alignof(gc_payload_word_t)));
    if (shape.permissions && shape.nodeCount > 0) {
        payload.permissions = static_cast<uint8_t *>(arena.alloc(
            camel::core::mm::alignUp(
                static_cast<size_t>(shape.nodeCount),
                alignof(gc_payload_word_t)),
            alignof(gc_payload_word_t)));
    }
    if (shape.attrs && shape.nodeCount > 0) {
        payload.attrs = static_cast<uint32_t *>(arena.alloc(
            camel::core::mm::alignUp(
                sizeof(uint32_t) * static_cast<size_t>(shape.nodeCount),
                alignof(gc_payload_word_t)),
            alignof(gc_payload_word_t)));
    }
    payload.payload          = arenaAllocArray<gc_payload_word_t>(arena, shape.payloadWordCount);
    payload.inputs           = arenaAllocArray<gc_node_ref_t>(arena, shape.edgeCount);
    payload.normPorts        = arenaAllocArray<gc_node_ref_t>(arena, shape.normPortCount);
    payload.withPorts        = arenaAllocArray<gc_node_ref_t>(arena, shape.withPortCount);
    payload.closure          = arenaAllocArray<gc_node_ref_t>(arena, shape.closureCount);
    payload.branchArms       = arenaAllocArray<GCBranchArm>(arena, shape.branchArmCount);
    payload.nodeCount        = shape.nodeCount;
    payload.edgeCount        = shape.edgeCount;
    payload.payloadWordCount = shape.payloadWordCount;
    payload.normPortCount    = shape.normPortCount;
    payload.withPortCount    = shape.withPortCount;
    payload.closureCount     = shape.closureCount;
    payload.branchArmCount   = shape.branchArmCount;
    return payload;
}

GCGraphPayloadArena makePayloadArenaView(GCGraphNativePayload &payload) {
    return GCGraphPayloadArena{
        .nodes            = payload.nodes,
        .origins          = payload.origins,
        .permissions      = payload.permissions,
        .attrs            = payload.attrs,
        .inputs           = payload.inputs,
        .payload          = payload.payload,
        .normPorts        = payload.normPorts,
        .withPorts        = payload.withPorts,
        .closure          = payload.closure,
        .branchArms       = payload.branchArms,
        .nodeCount        = payload.nodeCount,
        .edgeCount        = payload.edgeCount,
        .payloadWordCount = payload.payloadWordCount,
        .normPortCount    = payload.normPortCount,
        .withPortCount    = payload.withPortCount,
        .closureCount     = payload.closureCount,
        .branchArmCount   = payload.branchArmCount,
        .entryNode        = payload.entryNode,
        .exitNode         = payload.exitNode,
        .outputNode       = payload.outputNode,
        .returnNode       = payload.returnNode,
        .returnKind       = payload.returnKind,
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
    const FrameSlotTypes &frameSlots, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GCGraph *outerGraph,
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payloadShape,
    const std::function<void(GCGraphPayloadArena &)> &emitPayload,
    std::span<const slot_t> staticSlots) {
    const size_t bytes = requiredBytes(
        dependencies,
        subGraphs,
        staticGraphRefs,
        payloadShape,
        staticSlots,
        frameSlots);
    // Throws mm::MemoryExhausted when the graph space is at its limit.
    void *mem = mm::graphSpace().alloc(bytes, alignof(GCGraph));
    return constructInPlace(
        mem,
        bytes,
        debugRecord,
        funcType,
        frameSlots,
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
    std::span<const slot_t> staticSlots, const FrameSlotTypes &frameSlots) {
    return planGraphArenaLayout(
               dependencies,
               subGraphs,
               staticGraphRefs,
               payload,
               staticSlots,
               frameSlots)
        .totalBytes;
}

GCGraph *GCGraphBuildAccess::constructInPlace(
    void *memory, size_t bytes, GCGraphDebugRecord *debugRecord,
    camel::core::type::FunctionType *funcType, const FrameSlotTypes &frameSlots,
    camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
    GCGraph *outerGraph, std::span<GCGraph *const> dependencies,
    std::span<GCGraph *const> subGraphs, std::span<GCGraph *const> staticGraphRefs,
    const GCGraphPayloadShape &payloadShape,
    const std::function<void(GCGraphPayloadArena &)> &emitPayload,
    std::span<const slot_t> staticSlots) {
    EXEC_WHEN_DEBUG(
        mm::graphSpace().validateFreeList(
            std::format(
                "before constructing graph '{}'",
                debugRecord ? debugRecord->name : "<null>")));
    ASSERT(memory != nullptr, "GCGraph in-place construction requires valid memory.");
    CAMEL_LOG_INFO_S(
        "GCGraphBuild",
        "constructInPlace begin: name='{}' bytes={} deps={} subs={} staticRefs={} staticSlots={} "
        "nodes={} payloadWords={} edges={}.",
        debugRecord ? debugRecord->name : std::string{"<null>"},
        bytes,
        dependencies.size(),
        subGraphs.size(),
        staticGraphRefs.size(),
        staticSlots.size(),
        payloadShape.nodeCount,
        payloadShape.payloadWordCount,
        payloadShape.edgeCount);
    FixedBufferAllocator arena(memory, bytes);
    void *graphMem = arena.alloc(sizeof(GCGraph), alignof(GCGraph));
    ASSERT(graphMem == memory, "Graph arena must place GCGraph at the start of its owned block.");

    // The reference table: the three lists merged, each graph once, with the roles it has in
    // them (callees first, in their order, then nested graphs, then constants' graphs).
    const size_t refCapacity = dependencies.size() + subGraphs.size() + staticGraphRefs.size();
    auto *refStorage         = static_cast<GCGraph **>(
        arena.alloc(bytesForArray<GCGraph *>(refCapacity), alignof(GCGraph *)));
    auto *refRoleStorage = static_cast<uint8_t *>(
        arena.alloc(camel::core::mm::alignUp(refCapacity, alignof(GCGraph *)), alignof(GCGraph *)));
    std::unordered_map<const GCGraph *, uint32_t> refIndex;
    size_t refCount    = 0;
    const auto addRefs = [&](std::span<GCGraph *const> graphs, uint8_t role) {
        for (GCGraph *ref : graphs) {
            if (ref == nullptr) {
                continue;
            }
            const auto [it, added] = refIndex.emplace(ref, static_cast<uint32_t>(refCount));
            if (added) {
                refStorage[refCount]     = ref;
                refRoleStorage[refCount] = 0;
                ++refCount;
            }
            refRoleStorage[it->second] |= role;
        }
    };
    addRefs(dependencies, kGraphRefCallee);
    addRefs(subGraphs, kGraphRefNested);
    addRefs(staticGraphRefs, kGraphRefStatic);
    ::Tuple *staticArea               = buildStaticAreaInArena(arena, staticDataType, staticSlots);
    GCGraphNativePayload ownedPayload = allocPayloadInArena(arena, payloadShape);
    GCGraphPayloadArena payloadArena  = makePayloadArenaView(ownedPayload);
    payloadArena.refs                 = std::span<GCGraph *const>(refStorage, refCount);
    payloadArena.refIndex             = &refIndex;
    // The frame layout is part of the graph (see TupleType::constructFrameLayout); the padding
    // planned for it lets it start aligned wherever the arrays before it ended.
    TupleType *runtimeDataType = nullptr;
    if (frameSlots) {
        const size_t layoutBytes = TupleType::frameLayoutBytes(*frameSlots);
        void *layoutMem          = arena.alloc(layoutBytes, alignof(TupleType));
        runtimeDataType          = TupleType::constructFrameLayout(layoutMem, *frameSlots);
    }
    CAMEL_LOG_INFO_S(
        "GCGraphBuild",
        "constructInPlace allocated arenas: name='{}' staticArea={} nodes={} inputs={} "
        "branchArms={}.",
        debugRecord ? debugRecord->name : std::string{"<null>"},
        static_cast<const void *>(staticArea),
        static_cast<const void *>(ownedPayload.nodes),
        static_cast<const void *>(ownedPayload.inputs),
        static_cast<const void *>(ownedPayload.branchArms));
    emitPayload(payloadArena);
    CAMEL_LOG_INFO_S(
        "GCGraphBuild",
        "constructInPlace payload emitted: name='{}' entry={} exit={} output={} return={}.",
        debugRecord ? debugRecord->name : std::string{"<null>"},
        payloadArena.entryNode,
        payloadArena.exitNode,
        payloadArena.outputNode,
        payloadArena.returnNode);
    syncPayloadArenaView(ownedPayload, payloadArena);

    auto *graph = new (graphMem) GCGraph(
        debugRecord,
        funcType,
        runtimeDataType,
        staticDataType,
        closureType,
        outerGraph,
        refStorage,
        refRoleStorage,
        narrowIntegral<gc_cnt_t>(refCount, "graphs referred to"),
        ownedPayload,
        staticArea);

    for (GCGraph *subGraph : subGraphs) {
        if (subGraph) {
            subGraph->outerGraph_ = graph;
        }
    }
    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
        const GCNode *node = graph->node(ref);
        if (node->kind == GCNodeKind::Join) {
            const auto *body     = graph->nodeBodyAs<GCJoinBody>(ref);
            const auto armInputs = graph->withInputsOf(ref);
            if (body->armCount != armInputs.size()) {
                throw std::runtime_error(
                    std::format(
                        "Constructed graph '{}' has corrupted JOIN node {} immediately after "
                        "construction: armCount={} inputs={}.",
                        graph->name(),
                        ref,
                        body->armCount,
                        armInputs.size()));
            }
        }
    }
    ASSERT(arena.available() == 0, "Graph arena planning did not match graph emission.");
    EXEC_WHEN_DEBUG(
        mm::graphSpace().validateFreeList(
            std::format("constructing graph '{}'", debugRecord ? debugRecord->name : "<null>")));
    CAMEL_LOG_INFO_S(
        "GCGraphBuild",
        "constructInPlace finished: name='{}' graph={}.",
        debugRecord ? debugRecord->name : std::string{"<null>"},
        static_cast<const void *>(graph));
    return graph;
}

GCGraph::GCGraph(
    GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
    camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GCGraph *outerGraph, GCGraph **refs,
    uint8_t *refRoles, gc_cnt_t refCount, const GCGraphNativePayload &payload, ::Tuple *staticArea)
    : debug_(debugRecord), funcType_(funcType), runtimeDataType_(runtimeDataType),
      staticDataType_(staticDataType), closureType_(closureType), outerGraph_(outerGraph),
      refs_(refs), refRoles_(refRoles), refCount_(refCount), nativePayload_(payload),
      staticArea_(staticArea) {
    // A new graph: a lineage and a revision of its own (graph facts, facts.h). A commit that
    // encoded it as the copy of another graph replaces them (GCGraphBuildAccess::inheritIdentity).
    static std::atomic<uint64_t> serials{0};
    serial_  = serials.fetch_add(1, std::memory_order_relaxed) + 1;
    lineage_ = serial_;
}

GCGraphPayloadShape describeDraftNativePayload(const GraphDraft &draft) {
    return describeDraftPayload(draft, planDraftPayload(draft));
}

void emitDraftNativePayload(
    const GraphDraft &draft, GCGraphPayloadArena &payload,
    const std::function<GCGraph *(GCGraph *)> &resolveRuntimeGraph) {
    emitDraftPayload(draft, planDraftPayload(draft), payload, resolveRuntimeGraph);
}

void validateRuntimeGraphPayload(const GCGraph *graph) {
    validateRuntimeGraphPayloadImpl(graph);
    // The references are sound; now what the back ends assume about branches and types.
    validateRuntimeBranchArms(graph);
    validateRuntimeGraphTypes(graph);
}

void validateRuntimeGraphCalls(const GCGraph *graph) {
    if (!graph || !graph->hasNodePayload()) {
        return;
    }
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const GCNode *node = *it;
        if (node && node->kind == GCNodeKind::Loop) {
            // I13, I19 (docs/design/op-semantics 11.2): the node's inputs and the two graphs'
            // ports agree; the types are checked with the graph's types (I18).
            validateRuntimeLoopInterface(graph, it.ref());
            continue;
        }
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        const GCGraph *callee = graph->directCalleeGraphOf(it.ref());
        if (!callee || !callee->hasNodePayload()) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has FUNC node {} whose callee graph is not encoded.",
                    graph->name(),
                    it.ref()));
        }
        const size_t normArgs = graph->normInputsOf(it.ref()).size();
        const size_t withArgs = graph->withInputsOf(it.ref()).size();
        if (normArgs != callee->normPorts().size() || withArgs != callee->withPorts().size()) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has FUNC node {} passing {} arguments and {} 'with' "
                    "arguments to '{}', which takes {} and {}.",
                    graph->name(),
                    it.ref(),
                    normArgs,
                    withArgs,
                    callee->name(),
                    callee->normPorts().size(),
                    callee->withPorts().size()));
        }
    }
    // A graph refers to other graphs only through its reference table (design 3.7, I12): the
    // graphs the function constants of its static area point to are entries of it too.
    if (const TupleType *types = graph->staticDataType()) {
        const auto slots = graph->staticSlots();
        std::vector<GCGraph *> referenced;
        std::unordered_set<const Object *> visited;
        for (size_t i = 1; i < slots.size() && i < types->size(); ++i) {
            collectStaticGraphRefsRecursive(referenced, slots[i], types->typeAt(i), visited);
        }
        const auto refs = graph->refs();
        for (GCGraph *ref : referenced) {
            if (std::find(refs.begin(), refs.end(), ref) == refs.end()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has a function constant of graph '{}', which is not in "
                        "its reference table.",
                        graph->name(),
                        ref ? ref->name() : std::string("<null>")));
            }
        }
    }
}

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

std::span<const camel::core::SymId> GCGraph::captureNames() const {
    return debug_ ? std::span<const camel::core::SymId>(debug_->captureNames)
                  : std::span<const camel::core::SymId>();
}

uint32_t GCGraph::nodeAttrs(gc_node_ref_t ref) const {
    return containsNodeRef(ref) && nativePayload_.attrs ? nativePayload_.attrs[ref] : 0;
}

uint8_t GCGraph::nodePermissions(gc_node_ref_t ref) const {
    return containsNodeRef(ref) && nativePayload_.permissions ? nativePayload_.permissions[ref] : 0;
}

uint64_t GCGraph::nodeOrigin(gc_node_ref_t ref) const {
    return containsNodeRef(ref) && nativePayload_.origins ? nativePayload_.origins[ref] : 0;
}

camel::core::type::FunctionType *GCGraph::funcType() const { return funcType_; }

const TupleType *GCGraph::runtimeDataType() const { return runtimeDataType_; }

const TupleType *GCGraph::staticDataType() const { return staticDataType_; }

const TupleType *GCGraph::closureType() const { return closureType_; }

// A graph without constants has no static area; its frame layout is the runtime data type.
bool GCGraph::hasFrameLayout() const { return runtimeDataType() != nullptr; }

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

std::vector<GCGraph *> GCGraph::refsWithRole(uint8_t role) const {
    std::vector<GCGraph *> graphs;
    for (gc_cnt_t i = 0; i < refCount_; ++i) {
        if ((refRoles_[i] & role) != 0) {
            graphs.push_back(refs_[i]);
        }
    }
    return graphs;
}

std::span<const slot_t> GCGraph::staticSlots() const {
    return staticArea_ ? std::span<const slot_t>(staticArea_->data(), staticArea_->size())
                       : std::span<const slot_t>{};
}

bool GCGraph::hasNodePayload() const { return !nativePayload_.empty(); }

const GCGraphNativePayload *GCGraph::nodePayload() const {
    return nativePayload_.empty() ? nullptr : &nativePayload_;
}

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

gc_node_ref_t GCGraph::nodeRef(const GCNode *nodePtr) const {
    const GCNode *base = nativePayload_.nodes;
    if (base == nullptr || nodePtr < base || nodePtr >= base + nativePayload_.nodeCount) {
        return kInvalidNodeRef;
    }
    return static_cast<gc_node_ref_t>(nodePtr - base);
}

std::span<const std::byte> GCGraph::nodePayloadBytes(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (n == nullptr || !n->hasPayload()) {
        return {};
    }
    // The size follows from the kind (and for a FILL from its slot count), as the encoder
    // laid it out (draftPayloadWords): no scan for the next payload.
    const auto *body = reinterpret_cast<const std::byte *>(nativePayload_.payload + n->payload);
    size_t bytes     = 0;
    switch (n->kind) {
    case GCNodeKind::Func:
        bytes = sizeof(GCFuncRef);
        break;
    case GCNodeKind::Call:
        bytes = sizeof(GCCallBody);
        break;
    case GCNodeKind::Accs:
        bytes = sizeof(GCAccsBody);
        break;
    case GCNodeKind::Oper:
        bytes = sizeof(GCOperBody);
        break;
    case GCNodeKind::Fill:
        bytes = sizeof(GCFillBody) +
                sizeof(gc_slot_idx_t) * reinterpret_cast<const GCFillBody *>(body)->slotCount;
        break;
    case GCNodeKind::Brch:
        bytes = sizeof(GCBrchBody);
        break;
    case GCNodeKind::Join:
        bytes = sizeof(GCJoinBody);
        break;
    case GCNodeKind::Loop:
        bytes = sizeof(GCLoopRef);
        break;
    default:
        return {};
    }
    const size_t words = (bytes + kPayloadWordBytes - 1) / kPayloadWordBytes;
    return {body, words * kPayloadWordBytes};
}

GCGraph *GCGraph::directCalleeGraphOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Func || !n->hasPayload()) {
        return nullptr;
    }
    const uint32_t index = nodeBodyAs<GCFuncRef>(ref)->ref;
    return index < refCount_ ? refs_[index] : nullptr;
}

GCGraph *GCGraph::loopCondGraphOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Loop || !n->hasPayload()) {
        return nullptr;
    }
    const uint32_t index = nodeBodyAs<GCLoopRef>(ref)->condRef;
    return index < refCount_ ? refs_[index] : nullptr;
}

GCGraph *GCGraph::loopBodyGraphOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Loop || !n->hasPayload()) {
        return nullptr;
    }
    const uint32_t index = nodeBodyAs<GCLoopRef>(ref)->bodyRef;
    return index < refCount_ ? refs_[index] : nullptr;
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

GCUseIndex::GCUseIndex(const GCGraph &graph) {
    // Users in node order, each user once per group even when it reads the node twice: the
    // order and the uniqueness the encoder gave the stored outputs before they were dropped.
    // Two passes over the input table: count, then place (a counting sort by source).
    const size_t count  = graph.nodeCount();
    const auto groupsOf = [&](gc_node_ref_t user) {
        return std::array<std::span<const gc_node_ref_t>, 3>{
            graph.normInputsOf(user),
            graph.withInputsOf(user),
            graph.ctrlInputsOf(user)};
    };
    // A user reading one source twice in one group is listed once: it is skipped when the
    // previous entry placed for that (source, group) is the same user.
    std::vector<uint32_t> sizes(count * 3, 0);
    std::vector<gc_node_ref_t> lastUser(count * 3, kInvalidNodeRef);
    for (gc_node_ref_t user = 0; user < count; ++user) {
        const auto groups = groupsOf(user);
        for (size_t kind = 0; kind < 3; ++kind) {
            for (gc_node_ref_t input : groups[kind]) {
                const size_t at = static_cast<size_t>(input) * 3 + kind;
                if (lastUser[at] != user) {
                    lastUser[at] = user;
                    ++sizes[at];
                }
            }
        }
    }
    begins_.resize(count * 3 + 1);
    uint32_t total = 0;
    for (size_t at = 0; at < count * 3; ++at) {
        begins_[at] = total;
        total += sizes[at];
    }
    begins_[count * 3] = total;
    users_.resize(total);
    std::vector<uint32_t> cursor(begins_.begin(), begins_.end() - 1);
    std::fill(lastUser.begin(), lastUser.end(), kInvalidNodeRef);
    for (gc_node_ref_t user = 0; user < count; ++user) {
        const auto groups = groupsOf(user);
        for (size_t kind = 0; kind < 3; ++kind) {
            for (gc_node_ref_t input : groups[kind]) {
                const size_t at = static_cast<size_t>(input) * 3 + kind;
                if (lastUser[at] != user) {
                    lastUser[at]         = user;
                    users_[cursor[at]++] = user;
                }
            }
        }
    }
}

const GCUseIndex &GCGraph::uses() const {
    if (GCUseIndex *known = uses_.load(std::memory_order_acquire)) {
        return *known;
    }
    auto *built          = new GCUseIndex(*this);
    GCUseIndex *expected = nullptr;
    if (!uses_.compare_exchange_strong(
            expected,
            built,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        delete built; // another thread was first
        return *expected;
    }
    return *built;
}

GCGraph::~GCGraph() { delete uses_.load(std::memory_order_acquire); }

std::span<const gc_node_ref_t> GCGraph::normOutputsOf(gc_node_ref_t ref) const {
    return containsNodeRef(ref) ? uses().norm(ref) : std::span<const gc_node_ref_t>();
}

std::span<const gc_node_ref_t> GCGraph::withOutputsOf(gc_node_ref_t ref) const {
    return containsNodeRef(ref) ? uses().with(ref) : std::span<const gc_node_ref_t>();
}

std::span<const gc_node_ref_t> GCGraph::ctrlOutputsOf(gc_node_ref_t ref) const {
    return containsNodeRef(ref) ? uses().ctrl(ref) : std::span<const gc_node_ref_t>();
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
    const auto *body = nodeBodyAs<GCBrchBody>(brchRef);
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
    return otherGraph != nullptr && otherGraph->nativePayload_.nodes == nativePayload_.nodes &&
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
        refs_,
        refRoles_,
        refCount_,
        nativePayload_,
        staticArea_);
    graph->originalGraph_ = originalGraph_;
    graph->lineage_       = lineage_; // the same content: the same graph and revision
    graph->serial_        = serial_;
    for (size_t i = 0; i < kExtraSlotCount; ++i) {
        graph->extraSlots_[i] = extraSlots_[i];
    }
    return graph;
}

void GCGraph::print(std::ostream &os, const camel::core::type::Type *type) const {
    (void)type;
    os << "GCGraph(name=" << name() << ", nodes=" << nodeCount() << ")";
}

void GCGraph::updateRefs(
    const camel::core::rtdata::Object::RefRelocator &relocate,
    const camel::core::type::Type *type) {
    (void)type;
    if (staticArea_ && staticDataType()) {
        staticArea_->updateRefs(relocate, staticDataType());
    }
    // Root carriers are registered with the GC individually, but a graph kept alive only
    // through a Function value (a runtime-created graph such as an autodiff gradient) must
    // keep its whole closure alive: trace the same edges the closure walk (traceGraphs) uses
    // so referenced graphs and their static constants survive as long as this graph does.
    const auto traceEdge = [&relocate, this](GCGraph *&edge) {
        if (!edge) {
            return;
        }
        edge = static_cast<GCGraph *>(relocate(
            static_cast<camel::core::rtdata::Object *>(edge),
            nullptr,
            camel::core::rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = nullptr,
                .slotType  = nullptr,
                .ownerKind = "GCGraph",
                .slotName  = "graph-edge",
                .slotIndex = camel::core::rtdata::RefTraceInfo::npos,
            }));
    };
    for (gc_cnt_t i = 0; i < refCount_; ++i) {
        traceEdge(refs_[i]);
    }
    traceEdge(originalGraph_);
}

void GCGraphManager::replaceRoot(GCGraph *rootGraph) {
    std::lock_guard lock(mutex_);
    std::vector<GCGraph *> newGraphs =
        rootGraph ? collectReachableGraphs(rootGraph) : std::vector<GCGraph *>{};
    std::unordered_set<GCGraph *> keep(newGraphs.begin(), newGraphs.end());
    // A private copy still tracked (a back end's lowered program) names the program graph it was
    // copied from (GCGraph::originalGraph): that graph and what it refers to stay, owned as part
    // of the program, until a later replacement finds no copy pointing at them.
    for (GCGraph *copy : detached_) {
        GCGraph *original = copy ? copy->originalGraph_ : nullptr;
        if (original && !keep.contains(original)) {
            for (GCGraph *graph : collectReachableGraphs(original)) {
                if (keep.insert(graph).second) {
                    newGraphs.push_back(graph);
                }
            }
        }
    }

    for (GCGraph *graph : graphs_) {
        if (!graph || keep.contains(graph)) {
            continue;
        }
        delete graph->debug_;
        graph->~GCGraph();
        mm::graphSpace().free(graph);
    }

    root_   = rootGraph;
    graphs_ = std::move(newGraphs);
    rebuildGcRoots();
}

void GCGraphManager::rebuildGcRoots() {
    gcRoots_.clear();
    debugRecords_.clear();
    gcRoots_.reserve(graphs_.size() + detached_.size());
    for (GCGraph *graph : graphs_) {
        if (!graph) {
            continue;
        }
        gcRoots_.push_back(graph);
        if (graph->debug_) {
            debugRecords_.push_back(graph->debug_);
        }
    }
    for (GCGraph *graph : detached_) {
        gcRoots_.push_back(graph);
    }
    for (GCGraph *graph : pinned_) {
        gcRoots_.push_back(graph);
    }
}

void GCGraphManager::pin(std::span<GCGraph *const> graphs) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    for (GCGraph *graph : graphs) {
        if (graph && std::find(pinned_.begin(), pinned_.end(), graph) == pinned_.end()) {
            pinned_.push_back(graph);
            gcRoots_.push_back(graph);
        }
    }
}

void GCGraphManager::trackDetached(std::span<GCGraph *const> graphs) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    for (GCGraph *graph : graphs) {
        if (graph && std::find(detached_.begin(), detached_.end(), graph) == detached_.end()) {
            detached_.push_back(graph);
            gcRoots_.push_back(graph);
        }
    }
}

void GCGraphManager::releaseDetached() {
    std::lock_guard lock(mutex_);
    const std::unordered_set<GCGraph *> owned(graphs_.begin(), graphs_.end());
    for (GCGraph *graph : detached_) {
        if (owned.contains(graph)) {
            continue; // installed as part of the program since
        }
        delete graph->debug_;
        graph->~GCGraph();
        mm::graphSpace().free(graph);
    }
    detached_.clear();
    rebuildGcRoots();
}

void GCGraphManager::adoptRoot(GCGraph *rootGraph) {
    std::lock_guard lock(mutex_);
    if (root_ || !graphs_.empty()) {
        replaceRoot(rootGraph);
        return;
    }

    root_ = rootGraph;
    if (!root_) {
        return;
    }

    graphs_ = collectReachableGraphs(root_);
    rebuildGcRoots();
}

GCGraphManager::~GCGraphManager() {
    std::lock_guard lock(mutex_);
    releaseDetached();
    clear();
    for (GCGraph *graph : pinned_) {
        delete graph->debug_;
        graph->~GCGraph();
        mm::graphSpace().free(graph);
    }
    pinned_.clear();
    gcRoots_.clear();
}

std::vector<GCGraph *> GCGraphManager::roots() const {
    std::lock_guard lock(mutex_);
    if (!root_) {
        return {};
    }
    return {root_};
}

std::vector<GCGraph *> GCGraphManager::reachableFromRoots() const {
    std::lock_guard lock(mutex_);
    return collectReachableGraphs(root_);
}

void GCGraphManager::clear() {
    std::lock_guard lock(mutex_);
    for (GCGraph *graph : graphs_) {
        if (!graph) {
            continue;
        }
        delete graph->debug_;
        graph->~GCGraph();
        mm::graphSpace().free(graph);
    }
    gcRoots_.clear();
    debugRecords_.clear();
    graphs_.clear();
    root_ = nullptr;
    // The pinned graphs belong to no program: they stay, and stay traced.
    gcRoots_.insert(gcRoots_.end(), pinned_.begin(), pinned_.end());
}

GCGraph *GCGraphManager::root() const {
    std::lock_guard lock(mutex_);
    return root_;
}

void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, Type *type,
    std::unordered_set<const Object *> &visited) {
    if (!shouldTraverseStaticSlotType(type) || slot == NullSlot) {
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
                if (!shouldTraverseStaticSlotType(closureType->typeAt(i))) {
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
            if (!shouldTraverseStaticSlotType(tupleType->typeAt(i))) {
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
        if (!shouldTraverseStaticSlotType(arrayType->elemType())) {
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
            if (!shouldTraverseStaticSlotType(structType->typeAt(i))) {
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
        // A Map or a Set is a container of typed slots too.
        if (const auto *coll = camel::core::type::asCollectionType(type)) {
            camel::core::rtdata::table::forEachSlot(
                fromSlot<camel::core::rtdata::Table *>(slot),
                coll,
                [&](slot_t inner, Type *innerType) {
                    collectStaticGraphRefsRecursive(refs, inner, innerType, visited);
                });
        }
        break;
    }
}

} // namespace camel::runtime
