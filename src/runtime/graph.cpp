// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/loop.h"
#include "runtime/graph_build.h"

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/composite.h"
#include "camel/core/type/intern.h"
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

bool castMayFault(const camel::core::type::Type *target, const camel::core::type::Type *source) {
    if (target == nullptr || source == nullptr) {
        return true;
    }
    return target->castSafetyFrom(const_cast<camel::core::type::Type *>(source)) !=
           camel::core::type::CastSafety::Safe;
}

bool shapesAreFixed(const camel::core::type::Type *type) {
    if (type == nullptr) {
        return false;
    }
    const auto *array = dynamic_cast<const camel::core::type::ArrayType *>(type);
    if (array == nullptr) {
        return true;
    }
    if (array->openRank()) {
        return false;
    }
    for (const auto &extent : array->extents()) {
        if (!extent.isConstant()) {
            return false;
        }
    }
    return true;
}

bool nodeIsFreelySchedulable(
    GCNodeKind kind, camel::core::OperId oper, const camel::core::type::Type *castTarget,
    const camel::core::type::Type *castSource, bool fixedShapes) {
    switch (kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Copy:
    case GCNodeKind::Fill:
    case GCNodeKind::Accs: // reads a field of a value whose type has it: nothing to fail
    case GCNodeKind::Brch:
    case GCNodeKind::Join:
        return true;
    case GCNodeKind::Cast:
        return !castMayFault(castTarget, castSource);
    case GCNodeKind::Oper: {
        const auto &table = camel::core::OperatorTable::instance();
        const std::optional<camel::core::OperatorTraits> traits = table.find(oper);
        if (!table.mayBeRecomputed(oper) || !traits.has_value()) {
            return false;
        }
        return !traits->mayFault || (traits->elementwise && fixedShapes);
    }
    default:
        // A call, a function, a gate, a loop: an anchor that may be an effect, and nothing
        // refuses an order on it (docs/design/effects/edges.cn.md 4.6).
        return false;
    }
}

bool nodeIsFreelySchedulable(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    if (node == nullptr) {
        return false;
    }
    switch (node->kind) {
    case GCNodeKind::Oper: {
        return nodeIsFreelySchedulable(node->kind, graph.nodeBodyAs<GCOperBody>(ref)->oper);
    }
    case GCNodeKind::Cast: {
        const auto inputs = graph.normInputsOf(ref);
        const GCNode *source = inputs.size() == 1 ? graph.node(inputs[0]) : nullptr;
        return nodeIsFreelySchedulable(
            node->kind,
            camel::core::kNoOper,
            node->dataType(),
            source != nullptr ? source->dataType() : nullptr);
    }
    default:
        return nodeIsFreelySchedulable(node->kind, camel::core::kNoOper);
    }
}

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

size_t nodeChunkCount(size_t nodeCount) {
    return (nodeCount + kNodeChunkNodes - 1) / kNodeChunkNodes;
}

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

/// The TypeId a node record stores for `type`: the type's own id, which the type table handed out
/// when it interned the type. A type has no identity but its structure, so this is the id every
/// other occurrence of that type has too; a type the table does not hold is not named by a frozen
/// graph, and the assertion (debug builds) is what says which path let one through.
camel::core::type::TypeId typeIdOf(Type *type) {
    if (type == nullptr) {
        return camel::core::type::kNoTypeId;
    }
    // An open signature is not a type yet; `closed()` gives the interned signature of its parts
    // as they stand, so a graph records a type and not a builder's working object.
    Type *frozen = type->closed();
    ASSERT(
        frozen->isInterned(),
        "A frozen graph node names a type the type table does not hold: every type a graph "
        "records must come from the table's interning path (TypeTable::intern), not from an "
        "object built beside it.");
    return frozen->typeId();
}

struct GraphArenaLayout {
    size_t graphBytes       = 0;
    size_t refBytes         = 0; // the reference table
    size_t refRoleBytes     = 0; // its roles, padded to the next array's alignment
    size_t tupleBytes       = 0;
    size_t chunkDirBytes    = 0; // the directory of node chunks
    size_t regionBytes      = 0; // the region table
    size_t regionPortBytes  = 0; // the form ports of the regions
    size_t writingBytes     = 0; // the equivalences between nodes
    size_t orderEdgeBytes   = 0; // the order edges between nodes
    size_t syncEdgeBytes    = 0; // the sync edges between nodes
    size_t normPortBytes    = 0;
    size_t withPortBytes    = 0;
    size_t closureBytes     = 0;
    size_t frameLayoutBytes = 0;
    size_t totalBytes       = 0;
};

GraphArenaLayout planGraphArenaLayout(
    std::span<const GraphId> dependencies, std::span<const GraphId> subGraphs,
    std::span<const GraphId> staticGraphRefs, std::span<const GraphId> ruleGraphs,
    const GCGraphPayloadShape &payload, std::span<const slot_t> staticSlots,
    const FrameSlotTypes &frameSlots) {
    const size_t refCount =
        dependencies.size() + subGraphs.size() + staticGraphRefs.size() + ruleGraphs.size();
    GraphArenaLayout layout{
        .graphBytes = camel::core::mm::alignUp(sizeof(GCGraph), alignof(GCGraph)),
        // Room for every graph the four lists name; a graph named twice takes one entry and
        // leaves its room unused.
        .refBytes = camel::core::mm::alignUp(
            bytesForArray<GraphId>(refCount), alignof(GCGraph *)),
        .refRoleBytes = camel::core::mm::alignUp(refCount, alignof(GCGraph *)),
        .tupleBytes = staticSlots.empty()
                          ? 0
                          : camel::core::mm::alignUp(
                                sizeof(::Tuple) + sizeof(slot_t) * staticSlots.size(),
                                alignof(::Tuple)),
        .chunkDirBytes = camel::core::mm::alignUp(
            sizeof(GCNodeChunk *) * std::max<size_t>(nodeChunkCount(payload.nodeCount), 1),
            alignof(GCNodeChunk *)),
        .regionBytes     = bytesForArray<GCRegion>(payload.regionCount),
        .regionPortBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.regionPortCount),
            alignof(gc_node_ref_t)),
        .writingBytes = camel::core::mm::alignUp(
            bytesForArray<WritingUnion>(payload.writingCount),
            alignof(WritingUnion)),
        .orderEdgeBytes = camel::core::mm::alignUp(
            bytesForArray<GCGraphOrderEdge>(payload.orderEdgeCount),
            alignof(GCGraphOrderEdge)),
        .syncEdgeBytes = camel::core::mm::alignUp(
            bytesForArray<GCGraphSyncEdge>(payload.syncEdgeCount),
            alignof(GCGraphSyncEdge)),
        .normPortBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.normPortCount),
            alignof(gc_node_ref_t)),
        .withPortBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.withPortCount),
            alignof(gc_node_ref_t)),
        .closureBytes = camel::core::mm::alignUp(
            bytesForArray<gc_node_ref_t>(payload.closureCount),
            alignof(gc_node_ref_t)),
    };
    const size_t beforeFrameLayout =
        layout.graphBytes + layout.refBytes + layout.refRoleBytes + layout.tupleBytes +
        layout.chunkDirBytes + layout.regionBytes + layout.regionPortBytes + layout.writingBytes + layout.orderEdgeBytes + layout.syncEdgeBytes + layout.normPortBytes + layout.withPortBytes + layout.closureBytes;
    // The frame layout comes last. The arrays before it may end at any 4-byte boundary, so the
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

template <typename T> void pushUnique(std::vector<T> &items, T value) {
    if (std::find(items.begin(), items.end(), value) == items.end()) {
        items.push_back(value);
    }
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

/// The regions of a graph are a tree rooted at the body, every node is in one of them, and the
/// nodes a region names are nodes of the graph. (Which nodes may read which is checked once
/// control regions exist.)
void validateRuntimeRegions(const GCGraph *graph) {
    const gc_cnt_t count = graph->regionCount();
    const auto bad       = [&](std::string_view what, uint32_t region) {
        throw std::runtime_error(
            std::format("Runtime graph '{}' has region {} {}.", graph->name(), region, what));
    };
    const auto inGraph = [&](gc_node_ref_t ref) {
        return ref == kInvalidNodeRef || (graph->containsNodeRef(ref) &&
                                          graph->node(ref)->kind != GCNodeKind::Dead);
    };
    if (count != 0 && graph->region(0)->kind != GCRegionKind::Root) {
        bad("as its body, which is not a root", 0);
    }
    for (uint32_t r = 1; r < count; ++r) {
        const GCRegion &region = *graph->region(r);
        if (region.kind == GCRegionKind::Dead) {
            continue;
        }
        if (region.kind == GCRegionKind::Root || region.parent >= count) {
            bad("with a parent that is not a region", r);
        }
        // The parents lead to the body, without a cycle.
        {
            uint32_t up = r;
            for (uint32_t hops = 0; up != 0; ++hops) {
                if (hops > count) {
                    bad("that is in a cycle of regions", r);
                }
                up = graph->region(up)->parent;
            }
        }
        if (!inGraph(region.owner) || !inGraph(region.result) || !inGraph(region.callSite)) {
            bad("that names a node the graph does not have", r);
        }
        const bool control = region.kind == GCRegionKind::Arm ||
                             region.kind == GCRegionKind::LoopCond ||
                             region.kind == GCRegionKind::LoopBody;
        if (control != (region.owner != kInvalidNodeRef)) {
            bad("whose owner does not fit its kind", r);
        }
        if (region.portBegin + static_cast<uint64_t>(region.portCount) >
            graph->nodePayload()->regionPortCount) {
            bad("with ports outside the table", r);
        }
        for (gc_node_ref_t port : graph->regionPorts(r)) {
            // An annotation region keeps no port a rewrite removed; a control region needs all.
            if (!inGraph(port) || (control && port == kInvalidNodeRef)) {
                bad("with a port the graph does not have", r);
            }
        }
    }
    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
        const uint32_t region = graph->nodeRegion(ref);
        if (region != 0 && (region >= count || graph->region(region)->kind == GCRegionKind::Dead)) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has node {} in region {}, which is not there.",
                    graph->name(),
                    ref,
                    region));
        }
    }
}

void validateRuntimeGraphPayloadImpl(const GCGraph *graph) {
    if (!graph) {
        return;
    }
    if (!graph->hasNodePayload()) {
        throw std::runtime_error(
            std::format("Runtime graph '{}' has no native node payload.", graph->name()));
    }
    validateRuntimeRegions(graph);

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
        // The input runs are consecutive and inside the input table of the node's chunk; the
        // last run of a chunk ends with the table.
        const GCNodeChunk &chunk = *payload->chunks[ref >> kNodeChunkShift];
        const gc_off_t runsEnd   = (ref & (kNodeChunkNodes - 1)) + 1 < chunk.nodeCount
                                       ? (node + 1)->inBegin
                                       : chunk.edgeCount;
        if (!(node->inBegin <= node->withBegin && node->withBegin <= node->ctrlBegin &&
              node->ctrlBegin <= runsEnd && runsEnd <= chunk.edgeCount)) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has node {} with inputs outside the input table.",
                    graph->name(),
                    ref));
        }
        if (node->hasPayload() && node->payload >= chunk.payloadWordCount) {
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
        if (node->kind == GCNodeKind::Dead) {
            continue;
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
            if (!body || body->ref >= graph->refs().size() || !isValid(graph->refs()[body->ref]) ||
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
                return index < graph->refs().size() && isValid(graph->refs()[index]) &&
                       (graph->refRoles(index) & kGraphRefCallee) != 0;
            };
            const bool hasCond = camel::core::loopHasCondition(body->kind);
            // A loop in region form names no graph; its parts are regions of this graph.
            const bool regionForm = body->bodyRef == kNoLoopGraph && body->condRef == kNoLoopGraph;
            if (static_cast<uint8_t>(body->kind) >
                    static_cast<uint8_t>(camel::core::LoopKind::ForWhile) ||
                (!regionForm &&
                 (!validLoop(body->bodyRef) ||
                  (hasCond ? !validLoop(body->condRef) : body->condRef != kNoLoopGraph)))) {
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
            // A GATE is "let a value through once the sync chain has", and it waits through a
            // **sync predecessor** (docs/design/effects/edges.cn.md 5.3 -- it has no control input
            // there at all: it is a pure value-forwarding node with one data port and one sync
            // predecessor). The control input is what that wait used to be written as; the paths
            // this lane has not moved onto the sync edge yet still carry one, and the ones that
            // have carry the sync edge instead. A GATE with neither waits on nothing, which is
            // what this refuses.
            if (graph->syncPredsOf(ref).empty() && graph->ctrlInputsOf(ref).empty()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has GATE node {} without a wait: no sync predecessor "
                        "and no control input.",
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

struct PlannedDraftNode {
    bool dead               = false; // the place of an erased node (ids are kept)
    gc_node_ref_t draftId   = kInvalidNodeRef;
    gc_node_ref_t ref       = kInvalidNodeRef;
    gc_off_t payload        = kNoPayload; // word offset in the payload area
    size_t payloadWordCount = 0;
};

struct DraftPayloadPlan {
    std::vector<PlannedDraftNode> nodes;
    std::vector<gc_node_ref_t> runtimeRefsByDraftId;
    gc_cnt_t edgeCount        = 0;
    gc_cnt_t orderEdgeCount   = 0;
    gc_cnt_t syncEdgeCount    = 0;
    gc_cnt_t payloadWordCount = 0;
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
                    "(kind={}; the draft has {} places, {} nodes, keeps ids: {}).",
                    ref,
                    lane,
                    owner,
                    ownerHeader ? static_cast<int>(ownerHeader->kind) : -1,
                    draft.nodeSlotCount(),
                    draft.nodeCount(),
                    draft.keepsNodeIds()));
        }
    };

    DraftPayloadPlan plan;
    plan.runtimeRefsByDraftId.assign(draft.nodeSlotCount(), kInvalidNodeRef);

    // Live nodes keep their draft order and take dense NodeIds.
    size_t nodeCount      = 0;
    size_t edgeCount      = 0;
    size_t orderEdgeCount = 0;
    size_t syncEdgeCount  = 0;
    size_t payloadWords   = 0;
    const bool keepIds = draft.keepsNodeIds();
    for (gc_node_ref_t draftId = 0; draftId < draft.nodeSlotCount(); ++draftId) {
        if (!draft.alive(draftId)) {
            if (keepIds) {
                // The place stays, as a Dead record, so that the others keep their ids.
                plan.nodes.push_back(
                    PlannedDraftNode{
                        .dead    = true,
                        .draftId = draftId,
                        .ref     = static_cast<gc_node_ref_t>(nodeCount),
                    });
                ++nodeCount;
            }
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
        for (gc_node_ref_t ref : draft.orderInputsOf(draftId)) {
            requireAlive(draftId, ref, "order-input");
        }
        for (gc_node_ref_t ref : draft.syncInputsOf(draftId)) {
            requireAlive(draftId, ref, "sync-input");
        }
        const size_t words = draftPayloadWords(*header);
        // The NodeId kInvalidNodeRef and the payload offset kNoPayload are reserved.
        ASSERT(
            nodeCount < kInvalidNodeRef && payloadWords + words < kNoPayload,
            "A runtime graph is addressed with 32 bits; this one has no room left.");
        plan.nodes.push_back(
            PlannedDraftNode{
                .draftId = draftId,
                .ref     = static_cast<gc_node_ref_t>(nodeCount),
                .payload = words == 0 ? kNoPayload : static_cast<gc_off_t>(payloadWords),
                .payloadWordCount = words,
            });
        plan.runtimeRefsByDraftId[draftId] = static_cast<gc_node_ref_t>(nodeCount);
        ++nodeCount;
        payloadWords += words;
        edgeCount += draft.normInputsOf(draftId).size() + draft.withInputsOf(draftId).size() +
                     draft.ctrlInputsOf(draftId).size();
        orderEdgeCount += draft.orderInputsOf(draftId).size();
        syncEdgeCount += draft.syncInputsOf(draftId).size();
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

    plan.edgeCount        = static_cast<gc_cnt_t>(edgeCount);
    plan.orderEdgeCount   = static_cast<gc_cnt_t>(orderEdgeCount);
    plan.syncEdgeCount    = static_cast<gc_cnt_t>(syncEdgeCount);
    plan.payloadWordCount = static_cast<gc_cnt_t>(payloadWords);
    return plan;
}

GCGraphPayloadShape describeDraftPayload(const GraphDraft &draft, const DraftPayloadPlan &plan) {
    return GCGraphPayloadShape{
        .nodeCount        = static_cast<gc_cnt_t>(plan.nodes.size()),
        .edgeCount        = plan.edgeCount,
        .payloadWordCount = plan.payloadWordCount,
        .normPortCount    = static_cast<gc_cnt_t>(draft.normPorts().size()),
        .withPortCount    = static_cast<gc_cnt_t>(draft.withPorts().size()),
        .closureCount   = static_cast<gc_cnt_t>(draft.closureNodes().size()),
        .permissions    = std::any_of(
            plan.nodes.begin(),
            plan.nodes.end(),
            [&](const auto &planned) { return draft.nodePermissions(planned.draftId) != 0; }),
        .attrs = std::any_of(
            plan.nodes.begin(),
            plan.nodes.end(),
            [&](const auto &planned) { return draft.nodeAttrs(planned.draftId) != 0; }),
        .regions = std::any_of(
            plan.nodes.begin(),
            plan.nodes.end(),
            [&](const auto &planned) { return !planned.dead && draft.nodeRegion(planned.draftId) != 0; }),
        .regionCount     = static_cast<gc_cnt_t>(draft.regionCount()),
        .regionPortCount = static_cast<gc_cnt_t>(draft.regionPortCount()),
        .writingCount    = static_cast<gc_cnt_t>(draft.writings().size()),
        .orderEdgeCount  = plan.orderEdgeCount,
        .syncEdgeCount   = plan.syncEdgeCount,
        .base = draft.baseGraph(),
    };
}

void emitDraftPayload(
    const GraphDraft &draft, const DraftPayloadPlan &plan, GCGraphPayloadArena &payload) {
    ASSERT(
        payload.nodeCount == static_cast<gc_cnt_t>(plan.nodes.size()),
        "Draft payload arena shape does not match the planned node count.");

    size_t inputCursor       = 0;
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
        if (planned.dead) {
            GCNode &record   = payload.nodes[planned.ref];
            record.kind      = GCNodeKind::Dead;
            record.inBegin   = static_cast<gc_off_t>(inputCursor);
            record.withBegin = record.inBegin;
            record.ctrlBegin = record.inBegin;
            continue;
        }
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
        record.reserved  = 0;
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
        if (payload.regions) {
            payload.regions[planned.ref] = draft.nodeRegion(planned.draftId);
        }
        const GCNode &header = record;

        const auto payloadBytes = draft.payloadOf(planned.draftId);
        switch (draftHeader->kind) {
        case GCNodeKind::Func:
            if (!payloadBytes.empty()) {
                const GraphId callee = draft.calleeIdOf(planned.draftId);
                // The frozen node names the callee by its index in the reference table; a
                // callee that is not in it is a graph the encoder was not told this one calls.
                const auto *refIndex = payload.refIndex;
                const auto found =
                    refIndex ? refIndex->find(callee)
                             : std::unordered_map<GraphId, uint32_t>::const_iterator{};
                if (refIndex == nullptr || found == refIndex->end()) {
                    throw std::runtime_error(
                        std::format(
                            "FUNC node {} calls a graph that is not among the graphs this graph "
                            "calls (its reference table).",
                            planned.ref));
                }
                // The frozen node names the callee by its index in the reference table, and keeps
                // the rest of the draft's payload: which captured variables the call writes, and
                // so what its value carries the new version of (GCFuncRef::reserved).
                GCFuncRef body{.ref = found->second};
                if (payloadBytes.size_bytes() >= sizeof(body)) {
                    std::memcpy(&body, payloadBytes.data(), sizeof(body));
                    body.ref = found->second;
                }
                writeBody(payload.payload, header, body);
            }
            break;
        case GCNodeKind::Loop: {
            // The draft holds the two graphs; the frozen node names them by their indexes in
            // the reference table, as a FUNC node names its callee.
            ASSERT(
                payloadBytes.size_bytes() >= sizeof(GCLoopRef),
                "Draft LOOP payload is missing.");
            const GCLoopBody body = draft.loopGraphsOf(planned.draftId);
            const auto *refIndex  = payload.refIndex;
            const auto indexOf    = [&](GraphId target) -> uint32_t {
                if (!isValid(target)) {
                    return kNoLoopGraph;
                }
                const auto found = refIndex
                                          ? refIndex->find(target)
                                          : std::unordered_map<GraphId, uint32_t>::const_iterator{};
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
                    .iterableForm    = body.iterableForm,
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
            writeBody(
                payload.payload,
                header,
                GCBrchBody{
                    .join     = plan.runtimeRefsByDraftId[draftBody->join],
                    .armCount = draftBody->armCount,
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

    // The order edges and the sync edges (docs/design/effects/edges.cn.md 1.5): neither is a port,
    // so each is written to the graph's own array, in the node order the records have (which is
    // what makes the arrays sorted by user).
    const auto emitSideEdges = [&](auto *edges, gc_cnt_t count, const auto &inputsOf) {
        if (count == 0) {
            return;
        }
        size_t at = 0;
        for (const PlannedDraftNode &planned : plan.nodes) {
            if (planned.dead) {
                continue;
            }
            for (gc_node_ref_t pred : inputsOf(planned.draftId)) {
                ASSERT(at < count, "Draft side-edge emission wrote past the planned edges.");
                ASSERT(
                    pred < plan.runtimeRefsByDraftId.size(),
                    "Draft side-edge emission encountered a node outside the payload plan.");
                const gc_node_ref_t runtimePred = plan.runtimeRefsByDraftId[pred];
                ASSERT(
                    runtimePred != kInvalidNodeRef,
                    "Draft side-edge emission encountered a dead node.");
                edges[at] = {planned.ref, runtimePred};
                ++at;
            }
        }
        ASSERT(at == count, "Draft side-edge emission wrote the wrong count.");
    };
    emitSideEdges(
        payload.orderEdges, payload.orderEdgeCount,
        [&](gc_node_ref_t id) { return draft.orderInputsOf(id); });
    emitSideEdges(
        payload.syncEdges, payload.syncEdgeCount,
        [&](gc_node_ref_t id) { return draft.syncInputsOf(id); });

    for (size_t i = 0; i < draft.normPorts().size(); ++i) {
        payload.normPorts[i] = plan.runtimeRefsByDraftId[draft.normPorts()[i]];
    }
    for (size_t i = 0; i < draft.withPorts().size(); ++i) {
        payload.withPorts[i] = plan.runtimeRefsByDraftId[draft.withPorts()[i]];
    }
    for (size_t i = 0; i < draft.closureNodes().size(); ++i) {
        payload.closure[i] = plan.runtimeRefsByDraftId[draft.closureNodes()[i]];
    }

    // The regions: the node references in them follow the numbering of the nodes; one to a node
    // a rewrite erased becomes "none".
    const auto mapRegionNode = [&](gc_node_ref_t ref) {
        return ref < plan.runtimeRefsByDraftId.size() ? plan.runtimeRefsByDraftId[ref]
                                                       : kInvalidNodeRef;
    };
    for (uint32_t r = 0; r < draft.regionCount(); ++r) {
        GCRegion region        = draft.region(r);
        region.owner           = mapRegionNode(region.owner);
        region.result          = mapRegionNode(region.result);
        region.callSite        = mapRegionNode(region.callSite);
        payload.regionTable[r] = region;
    }
    for (uint32_t i = 0; i < draft.regionPortCount(); ++i) {
        payload.regionPorts[i] = mapRegionNode(draft.regionPort(i));
    }
    // The equivalences name nodes by the numbers the nodes have in this graph.
    for (size_t i = 0; i < draft.writings().size(); ++i) {
        WritingUnion entry = draft.writings()[i];
        entry.a            = mapRegionNode(entry.a);
        entry.b            = mapRegionNode(entry.b);
        ASSERT(
            entry.a != kInvalidNodeRef && entry.b != kInvalidNodeRef,
            "An equivalence names a node the graph does not have.");
        payload.writings[i] = entry;
    }

    // A graph without an entry starts at its first node.
    const auto firstNode = std::find_if(
        plan.nodes.begin(),
        plan.nodes.end(),
        [](const PlannedDraftNode &planned) { return !planned.dead; });
    payload.entryNode  = draft.entryNode() == kInvalidNodeRef
                             ? (firstNode == plan.nodes.end() ? kInvalidNodeRef : firstNode->ref)
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
}

} // namespace

std::vector<gc_node_ref_t> draftRuntimeNodeRefs(const GraphDraft &draft) {
    return planDraftPayload(draft).runtimeRefsByDraftId;
}

bool nodeIsFreelySchedulable(const GraphDraft &draft, gc_node_ref_t id, bool byShape) {
    const DraftNodeHeader *header = draft.header(id);
    if (header == nullptr || !draft.alive(id)) {
        return false;
    }
    switch (header->kind) {
    case GCNodeKind::Oper: {
        const std::span<const std::byte> payload = draft.payloadOf(id);
        bool fixed = byShape && shapesAreFixed(header->dataType);
        for (gc_node_ref_t input : draft.normInputsOf(id)) {
            const DraftNodeHeader *in = draft.header(input);
            fixed = fixed && in != nullptr && shapesAreFixed(in->dataType);
        }
        return payload.size() >= sizeof(GCOperBody) &&
               nodeIsFreelySchedulable(
                   header->kind,
                   reinterpret_cast<const GCOperBody *>(payload.data())->oper,
                   nullptr,
                   nullptr,
                   fixed);
    }
    case GCNodeKind::Cast: {
        const auto inputs = draft.normInputsOf(id);
        const DraftNodeHeader *source = inputs.size() == 1 ? draft.header(inputs[0]) : nullptr;
        return nodeIsFreelySchedulable(
            header->kind,
            camel::core::kNoOper,
            header->dataType,
            source != nullptr ? source->dataType : nullptr);
    }
    default:
        return nodeIsFreelySchedulable(header->kind, camel::core::kNoOper);
    }
}

bool isUnanchoredPure(const GraphDraft &draft, gc_node_ref_t node) {
    const DraftNodeHeader *header = draft.header(node);
    if (header == nullptr || !draft.alive(node)) {
        return false;
    }
    if (header->kind == GCNodeKind::Oper) {
        const std::span<const std::byte> payload = draft.payloadOf(node);
        if (payload.size() < sizeof(GCOperBody) ||
            !camel::core::OperatorTable::instance().mayBeRecomputed(
                reinterpret_cast<const GCOperBody *>(payload.data())->oper)) {
            return false;
        }
    } else if (header->kind != GCNodeKind::Cast) {
        return false;
    }
    const auto anyOf = [&](auto... groups) { return (!groups.empty() || ...); };
    return !anyOf(
        draft.ctrlInputsOf(node), draft.orderInputsOf(node), draft.syncInputsOf(node),
        draft.ctrlUsersOf(node), draft.orderUsersOf(node), draft.syncUsersOf(node));
}

bool carriesOrdering(const GraphDraft &draft, gc_node_ref_t node, bool byShape) {
    const DraftNodeHeader *header = draft.header(node);
    if (header == nullptr) {
        return false;
    }
    switch (header->kind) {
    case GCNodeKind::Func:
    case GCNodeKind::Call:
    case GCNodeKind::Join:
    case GCNodeKind::Brch:
    case GCNodeKind::Loop:
        return true;
    case GCNodeKind::Gate:
        // Either kind of wait is what makes a gate more than plain forwarding: the sync edge is
        // what the chain of a `sync` block writes, and the control input is the other kind of wait
        // such a gate was written with.
        return !draft.ctrlInputsOf(node).empty() || !draft.syncInputsOf(node).empty();
    case GCNodeKind::Dead:
    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
        return false; // not nodes of a graph a pass works on
    default:
        return !nodeIsFreelySchedulable(draft, node, byShape);
    }
}

void collectStaticGraphRefsRecursive(
    std::vector<GraphId> &refs, slot_t slot, Type *type,
    std::unordered_set<const Object *> &visited);

GCGraphDebugRecord *createGraphDebugRecord(std::string mangledName, std::string name) {
    auto *record        = new GCGraphDebugRecord();
    record->mangledName = std::move(mangledName);
    record->name        = std::move(name);
    return record;
}

void recordDraftDebugData(GCGraphDebugRecord *record, const GraphDraft &draft) {
    if (!record) {
        return;
    }
    record->implKernelUri = draft.implKernelUri();
    record->declarationFacts = draft.declarationFacts();
    record->captureNames.assign(draft.captureNames().begin(), draft.captureNames().end());
    record->nodeNames.clear();
    bool named = false;
    for (gc_node_ref_t ref = 0; ref < draft.nodeSlotCount(); ++ref) {
        if (draft.alive(ref) && draft.nodeName(ref) != camel::core::kEmptySym) {
            named = true;
        }
    }

    if (named) {
        // The names are indexed by runtime node id, not by draft place: the commit drops dead
        // places and may number the rest differently.
        const DraftPayloadPlan plan = planDraftPayload(draft);
        record->nodeNames.assign(plan.nodes.size(), camel::core::kEmptySym);

        for (gc_node_ref_t ref = 0; ref < draft.nodeSlotCount(); ++ref) {
            if (!draft.alive(ref) || draft.nodeName(ref) == camel::core::kEmptySym) {
                continue;
            }
            const gc_node_ref_t runtime =
                ref < plan.runtimeRefsByDraftId.size() ? plan.runtimeRefsByDraftId[ref]
                                                       : kInvalidNodeRef;

            if (runtime != kInvalidNodeRef && runtime < record->nodeNames.size()) {
                record->nodeNames[runtime] = draft.nodeName(ref);
            }
        }
    }
}

GCGraphNativePayload
allocPayloadInArena(FixedBufferAllocator &arena, const GCGraphPayloadShape &shape) {
    GCGraphNativePayload payload{};
    payload.chunkCount = static_cast<gc_cnt_t>(nodeChunkCount(shape.nodeCount));
    payload.chunks     = static_cast<GCNodeChunk **>(arena.alloc(
        camel::core::mm::alignUp(
            sizeof(GCNodeChunk *) * std::max<size_t>(payload.chunkCount, 1),
            alignof(GCNodeChunk *)),
        alignof(GCNodeChunk *)));
    std::fill_n(payload.chunks, std::max<size_t>(payload.chunkCount, 1), nullptr);
    payload.hasPermissions = shape.permissions;
    payload.hasAttrs       = shape.attrs;
    payload.hasRegions     = shape.regions;
    payload.regionTable    = arenaAllocArray<GCRegion>(arena, shape.regionCount);
    payload.regionCount    = shape.regionCount;
    payload.regionPorts    = arenaAllocArray<gc_node_ref_t>(arena, shape.regionPortCount);
    payload.regionPortCount = shape.regionPortCount;
    payload.writings        = arenaAllocArray<WritingUnion>(arena, shape.writingCount);
    payload.writingCount    = shape.writingCount;
    payload.orderEdges      = arenaAllocArray<GCGraphOrderEdge>(arena, shape.orderEdgeCount);
    payload.orderEdgeCount  = shape.orderEdgeCount;
    payload.syncEdges       = arenaAllocArray<GCGraphSyncEdge>(arena, shape.syncEdgeCount);
    payload.syncEdgeCount   = shape.syncEdgeCount;
    payload.normPorts        = arenaAllocArray<gc_node_ref_t>(arena, shape.normPortCount);
    payload.withPorts        = arenaAllocArray<gc_node_ref_t>(arena, shape.withPortCount);
    payload.closure          = arenaAllocArray<gc_node_ref_t>(arena, shape.closureCount);
    payload.nodeCount        = shape.nodeCount;
    payload.edgeCount        = shape.edgeCount;
    payload.payloadWordCount = shape.payloadWordCount;
    payload.normPortCount    = shape.normPortCount;
    payload.withPortCount    = shape.withPortCount;
    payload.closureCount     = shape.closureCount;
    return payload;
}

/// The arrays the encoder writes into while it numbers and links the nodes, with the offsets of
/// the whole graph. buildNodeChunks cuts them into the chunks the graph keeps.
struct PayloadStaging {
    std::vector<GCNode> nodes; // nodeCount + 1 records; the last is a sentinel
    std::vector<uint32_t> origins;
    std::vector<camel::core::sem::PermissionMask> permissions;
    std::vector<uint32_t> attrs;
    std::vector<uint32_t> regions;
    std::vector<gc_node_ref_t> inputs;
    std::vector<gc_payload_word_t> payload;
};

GCGraphPayloadArena makePayloadArenaView(GCGraphNativePayload &payload, PayloadStaging &staging) {
    return GCGraphPayloadArena{
        .nodes            = staging.nodes.data(),
        .origins          = staging.origins.data(),
        .permissions      = staging.permissions.empty() ? nullptr : staging.permissions.data(),
        .attrs            = staging.attrs.empty() ? nullptr : staging.attrs.data(),
        .regions          = staging.regions.empty() ? nullptr : staging.regions.data(),
        .regionTable      = payload.regionTable,
        .regionPorts      = payload.regionPorts,
        .regionCount      = payload.regionCount,
        .regionPortCount  = payload.regionPortCount,
        .writings         = payload.writings,
        .writingCount     = payload.writingCount,
        .orderEdges       = payload.orderEdges,
        .orderEdgeCount   = payload.orderEdgeCount,
        .syncEdges        = payload.syncEdges,
        .syncEdgeCount    = payload.syncEdgeCount,
        .inputs           = staging.inputs.data(),
        .payload          = staging.payload.data(),
        .normPorts        = payload.normPorts,
        .withPorts        = payload.withPorts,
        .closure          = payload.closure,
        .nodeCount        = payload.nodeCount,
        .edgeCount        = payload.edgeCount,
        .payloadWordCount = payload.payloadWordCount,
        .normPortCount    = payload.normPortCount,
        .withPortCount    = payload.withPortCount,
        .closureCount     = payload.closureCount,
        .entryNode        = payload.entryNode,
        .exitNode         = payload.exitNode,
        .outputNode       = payload.outputNode,
        .returnNode       = payload.returnNode,
        .returnKind       = payload.returnKind,
    };
}

void releaseNodeChunk(GCNodeChunk *chunk) {
    if (chunk->shares.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        chunk->~GCNodeChunk();
        mm::graphSpace().free(chunk);
    }
}

/// Whether `chunk` has exactly the content the staged nodes [first, first + count) would be cut
/// into: the same records (with their offsets in the chunk), columns, input table and payload.
bool chunkHolds(
    const GCNodeChunk &chunk, const PayloadStaging &staging, size_t first, size_t count,
    uint32_t edgeBegin, size_t edges, uint32_t payBegin, size_t words) {
    if (chunk.nodeCount != count || chunk.edgeCount != edges || chunk.payloadWordCount != words) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        GCNode record = staging.nodes[first + i];
        record.inBegin -= edgeBegin;
        record.withBegin -= edgeBegin;
        record.ctrlBegin -= edgeBegin;
        if (record.hasPayload()) {
            record.payload -= payBegin;
        }
        if (std::memcmp(&record, chunk.nodes() + i, sizeof(GCNode)) != 0) {
            return false;
        }
    }
    const auto same = [](const auto *x, const auto *y, size_t n) {
        return n == 0 || std::memcmp(x, y, n * sizeof(*x)) == 0;
    };
    return same(chunk.origins, staging.origins.data() + first, count) &&
           (!chunk.permissions || same(chunk.permissions, staging.permissions.data() + first, count)) &&
           (!chunk.attrs || same(chunk.attrs, staging.attrs.data() + first, count)) &&
           (!chunk.regions || same(chunk.regions, staging.regions.data() + first, count)) &&
           same(chunk.edges, staging.inputs.data() + edgeBegin, edges) &&
           same(chunk.payload, staging.payload.data() + payBegin, words);
}

/// Cuts the staged arrays into chunks of kNodeChunkNodes nodes, in the graph space. Offsets of
/// a chunk's records become offsets in that chunk's arrays.
void buildNodeChunks(
    GCGraphNativePayload &payload, const PayloadStaging &staging,
    const GCGraphPayloadShape &shape) {
    const size_t nodeCount = shape.nodeCount;
    // The payload words of the nodes from i on start at nextPayload[i]: the offsets grow with
    // the nodes that have a payload.
    std::vector<uint32_t> nextPayload(nodeCount + 1, shape.payloadWordCount);
    for (size_t i = nodeCount; i-- > 0;) {
        nextPayload[i] =
            staging.nodes[i].hasPayload() ? staging.nodes[i].payload : nextPayload[i + 1];
    }
    const auto align8 = [](size_t bytes) {
        return camel::core::mm::alignUp(bytes, alignof(gc_payload_word_t));
    };
    // The chunks of the graph the draft came from can stand for the new ones that equal them,
    // when the two graphs have the same columns.
    const GCGraphNativePayload *old = shape.base ? shape.base->nodePayload() : nullptr;
    if (old && (old->hasPermissions != shape.permissions || old->hasAttrs != shape.attrs ||
                old->hasRegions != shape.regions)) {
        old = nullptr;
    }
    size_t built = 0;
    try {
        for (; built < payload.chunkCount; ++built) {
            const size_t first       = built * kNodeChunkNodes;
            const size_t count       = std::min<size_t>(kNodeChunkNodes, nodeCount - first);
            const uint32_t edgeBegin = staging.nodes[first].inBegin;
            const uint32_t edgeEnd   = staging.nodes[first + count].inBegin;
            const uint32_t payBegin  = nextPayload[first];
            const uint32_t payEnd    = nextPayload[first + count];
            const size_t edges       = edgeEnd - edgeBegin;
            const size_t words       = payEnd - payBegin;

            if (old && built < old->chunkCount) {
                GCNodeChunk *known = old->chunks[built];
                if (chunkHolds(*known, staging, first, count, edgeBegin, edges, payBegin, words)) {
                    known->shares.fetch_add(1, std::memory_order_relaxed);
                    payload.chunks[built] = known;
                    continue;
                }
            }

            const size_t recordsAt = sizeof(GCNodeChunk);
            const size_t originsAt = align8(recordsAt + count * sizeof(GCNode));
            const size_t permsAt   = originsAt + align8(count * sizeof(uint32_t));
            const size_t attrsAt   = permsAt + (shape.permissions ? align8(count * sizeof(camel::core::sem::PermissionMask)) : 0);
            const size_t regionsAt = attrsAt + (shape.attrs ? align8(count * sizeof(uint32_t)) : 0);
            const size_t edgesAt = regionsAt + (shape.regions ? align8(count * sizeof(uint32_t)) : 0);
            const size_t payloadAt = edgesAt + align8(edges * sizeof(gc_node_ref_t));
            const size_t bytes     = payloadAt + words * sizeof(gc_payload_word_t);
            auto *base =
                static_cast<std::byte *>(mm::graphSpace().alloc(bytes, alignof(GCNodeChunk)));
            auto *chunk             = new (base) GCNodeChunk();
            chunk->nodeCount        = static_cast<uint32_t>(count);
            chunk->edgeCount        = static_cast<uint32_t>(edges);
            chunk->payloadWordCount = static_cast<uint32_t>(words);
            chunk->origins          = reinterpret_cast<uint32_t *>(base + originsAt);
            chunk->permissions =
                shape.permissions ? reinterpret_cast<camel::core::sem::PermissionMask *>(base + permsAt) : nullptr;
            chunk->attrs   = shape.attrs ? reinterpret_cast<uint32_t *>(base + attrsAt) : nullptr;
            chunk->regions = shape.regions ? reinterpret_cast<uint32_t *>(base + regionsAt) : nullptr;
            chunk->edges   = reinterpret_cast<gc_node_ref_t *>(base + edgesAt);
            chunk->payload = reinterpret_cast<gc_payload_word_t *>(base + payloadAt);

            GCNode *records = chunk->nodes();
            for (size_t i = 0; i < count; ++i) {
                GCNode record = staging.nodes[first + i];
                record.inBegin -= edgeBegin;
                record.withBegin -= edgeBegin;
                record.ctrlBegin -= edgeBegin;
                if (record.hasPayload()) {
                    record.payload -= payBegin;
                }
                records[i] = record;
            }
            std::memcpy(chunk->origins, staging.origins.data() + first, count * sizeof(uint32_t));
            if (chunk->permissions) {
                std::memcpy(
                    chunk->permissions,
                    staging.permissions.data() + first,
                    count * sizeof(camel::core::sem::PermissionMask));
            }
            if (chunk->attrs) {
                std::memcpy(chunk->attrs, staging.attrs.data() + first, count * sizeof(uint32_t));
            }
            if (chunk->regions) {
                std::memcpy(
                    chunk->regions,
                    staging.regions.data() + first,
                    count * sizeof(uint32_t));
            }
            if (edges != 0) {
                std::memcpy(
                    chunk->edges,
                    staging.inputs.data() + edgeBegin,
                    edges * sizeof(gc_node_ref_t));
            }
            if (words != 0) {
                std::memcpy(
                    chunk->payload,
                    staging.payload.data() + payBegin,
                    words * sizeof(gc_payload_word_t));
            }
            payload.chunks[built] = chunk;
        }
    } catch (...) {
        for (size_t i = 0; i < built; ++i) {
            releaseNodeChunk(payload.chunks[i]);
            payload.chunks[i] = nullptr;
        }
        throw;
    }
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
    GraphId id, const GCGraphDerivation &derivation, GCGraphDebugRecord *debugRecord,
    camel::core::type::FunctionType *funcType, const FrameSlotTypes &frameSlots,
    camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
    GraphId outerGraph, std::span<const GraphId> dependencies, std::span<const GraphId> subGraphs,
    std::span<const GraphId> staticGraphRefs, std::span<const GraphId> ruleGraphs,
    const GCGraphPayloadShape &payloadShape,
    const std::function<void(GCGraphPayloadArena &)> &emitPayload,
    std::span<const slot_t> staticSlots) {
    const size_t bytes = requiredBytes(
        dependencies,
        subGraphs,
        staticGraphRefs,
        ruleGraphs,
        payloadShape,
        staticSlots,
        frameSlots);
    // Throws mm::MemoryExhausted when the graph space is at its limit.
    void *mem = mm::graphSpace().alloc(bytes, alignof(GCGraph));
    return constructInPlace(
        mem,
        bytes,
        id,
        derivation,
        debugRecord,
        funcType,
        frameSlots,
        staticDataType,
        closureType,
        outerGraph,
        dependencies,
        subGraphs,
        staticGraphRefs,
        ruleGraphs,
        payloadShape,
        emitPayload,
        staticSlots);
}

size_t GCGraphBuildAccess::requiredBytes(
    std::span<const GraphId> dependencies, std::span<const GraphId> subGraphs,
    std::span<const GraphId> staticGraphRefs, std::span<const GraphId> ruleGraphs,
    const GCGraphPayloadShape &payload, std::span<const slot_t> staticSlots,
    const FrameSlotTypes &frameSlots) {
    return planGraphArenaLayout(
               dependencies,
               subGraphs,
               staticGraphRefs,
               ruleGraphs,
               payload,
               staticSlots,
               frameSlots)
        .totalBytes;
}

GCGraph *GCGraphBuildAccess::constructInPlace(
    void *memory, size_t bytes, GraphId id, const GCGraphDerivation &derivation,
    GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
    const FrameSlotTypes &frameSlots, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GraphId outerGraph,
    std::span<const GraphId> dependencies, std::span<const GraphId> subGraphs,
    std::span<const GraphId> staticGraphRefs, std::span<const GraphId> ruleGraphs,
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
        "constructInPlace begin: name='{}' bytes={} deps={} subs={} staticRefs={} ruleRefs={} "
        "staticSlots={} nodes={} payloadWords={} edges={}.",
        debugRecord ? debugRecord->name : std::string{"<null>"},
        bytes,
        dependencies.size(),
        subGraphs.size(),
        staticGraphRefs.size(),
        ruleGraphs.size(),
        staticSlots.size(),
        payloadShape.nodeCount,
        payloadShape.payloadWordCount,
        payloadShape.edgeCount);
    FixedBufferAllocator arena(memory, bytes);
    void *graphMem = arena.alloc(sizeof(GCGraph), alignof(GCGraph));
    ASSERT(graphMem == memory, "Graph arena must place GCGraph at the start of its owned block.");

    // The reference table: the four lists merged, each graph once, with the roles it has in
    // them (callees first, in their order, then nested graphs, then constants' graphs, then the
    // graphs a declaration tag names).
    const size_t refCapacity =
        dependencies.size() + subGraphs.size() + staticGraphRefs.size() + ruleGraphs.size();
    auto *refStorage         = static_cast<GraphId *>(arena.alloc(
        camel::core::mm::alignUp(bytesForArray<GraphId>(refCapacity), alignof(GCGraph *)),
        alignof(GCGraph *)));
    auto *refRoleStorage = static_cast<uint8_t *>(
        arena.alloc(camel::core::mm::alignUp(refCapacity, alignof(GCGraph *)), alignof(GCGraph *)));
    std::unordered_map<GraphId, uint32_t> refIndex;
    size_t refCount    = 0;
    const auto addRefs = [&](std::span<const GraphId> graphs, uint8_t role) {
        for (GraphId ref : graphs) {
            if (!isValid(ref)) {
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
    // A graph made from another keeps the order of that graph's table for the graphs it still
    // refers to, so that payloads which index the table (FUNC, LOOP) stay the same and the
    // node chunks that hold them can be shared.
    if (payloadShape.base) {
        std::unordered_set<GraphId> named;
        for (const auto list : {dependencies, subGraphs, staticGraphRefs, ruleGraphs}) {
            named.insert(list.begin(), list.end());
        }
        for (GraphId ref : payloadShape.base->refs()) {
            if (isValid(ref) && named.contains(ref)) {
                const auto [it, added] = refIndex.emplace(ref, static_cast<uint32_t>(refCount));
                if (added) {
                    refStorage[refCount]     = ref;
                    refRoleStorage[refCount] = 0;
                    ++refCount;
                }
            }
        }
    }
    addRefs(dependencies, kGraphRefCallee);
    addRefs(subGraphs, kGraphRefNested);
    addRefs(staticGraphRefs, kGraphRefStatic);
    addRefs(ruleGraphs, kGraphRefRule);
    ::Tuple *staticArea               = buildStaticAreaInArena(arena, staticDataType, staticSlots);
    GCGraphNativePayload ownedPayload = allocPayloadInArena(arena, payloadShape);
    PayloadStaging staging;
    staging.nodes.resize(static_cast<size_t>(payloadShape.nodeCount) + 1);
    staging.origins.resize(payloadShape.nodeCount);
    if (payloadShape.permissions) {
        staging.permissions.resize(payloadShape.nodeCount);
    }
    if (payloadShape.attrs) {
        staging.attrs.resize(payloadShape.nodeCount);
    }
    if (payloadShape.regions) {
        staging.regions.resize(payloadShape.nodeCount);
    }
    staging.inputs.resize(payloadShape.edgeCount);
    staging.payload.resize(payloadShape.payloadWordCount);
    GCGraphPayloadArena payloadArena = makePayloadArenaView(ownedPayload, staging);
    payloadArena.refs                 = std::span<const GraphId>(refStorage, refCount);
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
        "constructInPlace allocated arenas: name='{}' staticArea={} chunks={}.",
        debugRecord ? debugRecord->name : std::string{"<null>"},
        static_cast<const void *>(staticArea),
        ownedPayload.chunkCount);
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
    buildNodeChunks(ownedPayload, staging, payloadShape);

    auto *graph = new (graphMem) GCGraph(
        id,
        derivation,
        debugRecord,
        funcType,
        runtimeDataType,
        staticDataType,
        closureType,
        outerGraph,
        refStorage,
        refRoleStorage,
        static_cast<gc_cnt_t>(refCount),
        ownedPayload,
        staticArea);

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
    GraphId id, const GCGraphDerivation &derivation, GCGraphDebugRecord *debugRecord,
    camel::core::type::FunctionType *funcType, camel::core::type::TupleType *runtimeDataType,
    camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
    GraphId outerGraph, GraphId *refs, uint8_t *refRoles, gc_cnt_t refCount,
    const GCGraphNativePayload &payload, ::Tuple *staticArea)
    : id_(id), typeSpecializationRoot_(derivation.typeSpecializationRoot),
      flags_(
          static_cast<uint8_t>(
              (derivation.flags & ~kGraphFlagHasWritings) |
              (payload.writingCount != 0 ? kGraphFlagHasWritings : 0))),
      debug_(debugRecord), funcType_(funcType), runtimeDataType_(runtimeDataType),
      staticDataType_(staticDataType), closureType_(closureType), outerGraphId_(outerGraph),
      refs_(refs), refRoles_(refRoles), refCount_(refCount), nativePayload_(payload),
      staticArea_(staticArea) {
    // A new graph: a revision of its own (graph facts, facts.h). A commit that encoded it as
    // the copy of an unchanged graph replaces it (GCGraphBuildAccess::inheritRevision).
    static std::atomic<uint64_t> revisions{0};
    revision_ = revisions.fetch_add(1, std::memory_order_relaxed) + 1;
}

GCGraphPayloadShape describeDraftNativePayload(const GraphDraft &draft) {
    return describeDraftPayload(draft, planDraftPayload(draft));
}

void emitDraftNativePayload(const GraphDraft &draft, GCGraphPayloadArena &payload) {
    emitDraftPayload(draft, planDraftPayload(draft), payload);
}

void validateRuntimeGraphPayload(const Program &program, const GCGraph *graph) {
    validateRuntimeGraphPayloadImpl(graph);
    // The references are sound; now what the back ends assume about branches and types.
    validateRuntimeBranchArms(program, graph);
    validateRuntimeGraphTypes(graph);
}

void validateRuntimeGraphCalls(const Program &program, const GCGraph *graph, GraphState state) {
    if (!graph || !graph->hasNodePayload()) {
        return;
    }
    validateRuntimeGraphEdges(program, graph, state);
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const GCNode *node = *it;
        if (node && node->kind == GCNodeKind::Loop) {
            // I13, I19 (docs/design/op-semantics 11.2): the node's inputs and the two graphs'
            // ports agree; the types are checked with the graph's types (I18).
            validateRuntimeLoopInterface(program, graph, it.ref());
            continue;
        }
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        const GCGraph *callee = program.calleeOf(*graph, it.ref());
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
        std::vector<GraphId> referenced;
        std::unordered_set<const Object *> visited;
        for (size_t i = 1; i < slots.size() && i < types->size(); ++i) {
            collectStaticGraphRefsRecursive(referenced, slots[i], types->typeAt(i), visited);
        }
        const auto refs = graph->refs();
        for (GraphId ref : referenced) {
            if (std::find(refs.begin(), refs.end(), ref) == refs.end()) {
                const GCGraph *named = program.graph(ref);
                throw std::runtime_error(std::format(
                    "Runtime graph '{}' has a function constant of graph '{}', which is not in "
                    "its reference table.",
                    graph->name(),
                    named ? named->name() : std::format("#{}", toValue(ref))));
            }
        }
    }
}

const std::string &GCGraph::mangledName() const {
    static const std::string kEmpty;
    return debug_ ? debug_->mangledName : kEmpty;
}

const std::string &GCGraph::name() const {
    static const std::string kEmpty;
    return debug_ ? debug_->name : kEmpty;
}

const std::string &GCGraph::implKernelUri() const {
    static const std::string kEmpty;
    return debug_ ? debug_->implKernelUri : kEmpty;
}

uint32_t GCGraph::declarationFacts() const {
    return debug_ ? debug_->declarationFacts : 0;
}

std::span<const camel::core::SymId> GCGraph::captureNames() const {
    return debug_ ? std::span<const camel::core::SymId>(debug_->captureNames)
                  : std::span<const camel::core::SymId>();
}

std::string_view GCGraph::nodeName(gc_node_ref_t ref) const {
    if (!debug_ || ref >= debug_->nodeNames.size()) {
        return {};
    }
    const camel::core::SymId name = debug_->nodeNames[ref];
    return name == camel::core::kEmptySym ? std::string_view()
                                          : camel::core::symbolText(name);
}

camel::core::SymId GCGraph::nodeNameSym(gc_node_ref_t ref) const {
    if (!debug_ || ref >= debug_->nodeNames.size()) {
        return camel::core::kEmptySym;
    }
    return debug_->nodeNames[ref];
}

bool GCGraph::hasNodeNames() const { return debug_ && !debug_->nodeNames.empty(); }

uint32_t GCGraph::nodeAttrs(gc_node_ref_t ref) const {
    return containsNodeRef(ref) && chunkOf(ref).attrs ? chunkOf(ref).attrs[ref & (kNodeChunkNodes - 1)] : 0;
}

camel::core::sem::PermissionMask GCGraph::nodePermissions(gc_node_ref_t ref) const {
    return containsNodeRef(ref) && chunkOf(ref).permissions
               ? chunkOf(ref).permissions[ref & (kNodeChunkNodes - 1)]
               : 0;
}

uint64_t GCGraph::nodeOrigin(gc_node_ref_t ref) const {
    return containsNodeRef(ref) ? chunkOf(ref).origins[ref & (kNodeChunkNodes - 1)] : 0;
}

camel::core::type::FunctionType *GCGraph::funcType() const { return funcType_; }

uint32_t funcWriteMaskOf(const GCGraph &graph, gc_node_ref_t ref) {
    if (!graph.hasNodePayload() || !graph.containsNodeRef(ref)) {
        return 0;
    }
    const GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != GCNodeKind::Func || !node->hasPayload()) {
        return 0;
    }
    return graph.nodeBodyAs<GCFuncRef>(ref)->reserved;
}

camel::core::type::Type *callValueTypeOf(const GCGraph &callee, uint32_t writtenCaptures) {
    camel::core::type::FunctionType *calleeType = callee.funcType();
    camel::core::type::Type *exit = calleeType != nullptr ? calleeType->exitType() : nullptr;
    if (writtenCaptures == 0) {
        return exit;
    }
    const bool hasExit = exit != nullptr && exit->code() != camel::core::type::TypeCode::Void;
    // The capture ports are the with-ports after the declared ones, in the order the captures
    // were made: bit k of the mask is the k-th of them.
    const size_t declaredWith =
        calleeType != nullptr ? calleeType->withTypesCount() : size_t{0};
    const std::span<const gc_node_ref_t> withPorts = callee.withPorts();
    std::vector<camel::core::type::Type *> parts;
    if (hasExit) {
        parts.push_back(exit);
    }
    for (size_t k = 0; k < 32; ++k) {
        if ((writtenCaptures & (uint32_t{1} << k)) == 0) {
            continue;
        }
        const size_t port = declaredWith + k;
        const GCNode *capture = port < withPorts.size() ? callee.node(withPorts[port]) : nullptr;
        if (capture != nullptr && capture->dataType() != nullptr) {
            parts.push_back(capture->dataType());
        }
    }
    if (parts.size() == 1) {
        return parts.front();
    }
    if (parts.empty()) {
        return exit;
    }
    return camel::core::type::TupleType::create(parts);
}

const TupleType *GCGraph::runtimeDataType() const { return runtimeDataType_; }

const TupleType *GCGraph::staticDataType() const { return staticDataType_; }

const TupleType *GCGraph::closureType() const { return closureType_; }

// A graph without constants has no static area; its frame layout is the runtime data type.
bool GCGraph::hasFrameLayout() const { return runtimeDataType() != nullptr; }

size_t GCGraph::frameSize() const {
    ASSERT(hasFrameLayout(), "Runtime graph frame layout is not available.");
    return sizeof(camel::core::context::Frame) + sizeof(slot_t) * runtimeDataType()->size();
}

bool GCGraph::isMacro() const { return (flags_ & kGraphFlagMacro) != 0; }

bool GCGraph::isRoot() const { return !isValid(outerGraphId_); }

std::vector<GraphId> GCGraph::refsWithRole(uint8_t role) const {
    std::vector<GraphId> graphs;
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

std::optional<std::pair<int64_t, int64_t>>
GCGraph::intervalPairValuesOf(gc_node_ref_t ref) const {
    // The pair is a value: the FILL the front end builds and the constant a pass may replace it
    // with hold the same two integers, so the loop's bounds are read from either. This reads the
    // constant: a DATA node of the static area whose type is the tuple of the two bounds.
    const GCNode *n      = node(ref);
    const auto *nodeType = n == nullptr ? nullptr : n->dataType();
    if (n == nullptr || n->kind != GCNodeKind::Data || n->dataIndex >= 0 || nodeType == nullptr ||
        nodeType->code() != TypeCode::Tuple) {
        return std::nullopt;
    }
    const auto *type = static_cast<const TupleType *>(nodeType);
    if (type->size() != 2 || type->typeAt(0) == nullptr || type->typeAt(1) == nullptr ||
        type->typeAt(0)->code() != TypeCode::Int64 ||
        type->typeAt(1)->code() != TypeCode::Int64) {
        return std::nullopt;
    }
    const auto slots   = staticSlots();
    const size_t index = static_cast<size_t>(-static_cast<int64_t>(n->dataIndex));
    if (index == 0 || index >= slots.size() || slots[index] == NullSlot) {
        return std::nullopt;
    }
    const auto *tuple = fromSlot<::Tuple *>(slots[index]);
    if (tuple == nullptr || tuple->size() != 2) {
        return std::nullopt;
    }
    return std::pair<int64_t, int64_t>{
        fromSlot<int64_t>(tuple->get<slot_t>(0)), fromSlot<int64_t>(tuple->get<slot_t>(1))};
}

bool GCGraph::hasNodePayload() const { return !nativePayload_.empty(); }

const GCGraphNativePayload *GCGraph::nodePayload() const {
    return nativePayload_.empty() ? nullptr : &nativePayload_;
}


const GCNode *GCGraph::entryNode() const { return node(entryNodeRef()); }

gc_node_ref_t GCGraph::entryNodeRef() const { return nativePayload_.entryNode; }

const GCNode *GCGraph::exitNode() const { return node(exitNodeRef()); }

gc_node_ref_t GCGraph::exitNodeRef() const { return nativePayload_.exitNode; }

const GCNode *GCGraph::outputNode() const { return node(outputNodeRef()); }

gc_node_ref_t GCGraph::outputNodeRef() const { return nativePayload_.outputNode; }

const GCNode *GCGraph::returnNode() const { return node(returnNodeRef()); }

gc_node_ref_t GCGraph::returnNodeRef() const { return nativePayload_.returnNode; }

GCReturnKind GCGraph::returnKind() const { return nativePayload_.returnKind; }

gc_cnt_t GCGraph::liveNodeCount() const {
    gc_cnt_t live = 0;
    for (gc_cnt_t c = 0; c < nativePayload_.chunkCount; ++c) {
        const GCNodeChunk &chunk = *nativePayload_.chunks[c];
        for (uint32_t i = 0; i < chunk.nodeCount; ++i) {
            live += chunk.nodes()[i].kind != GCNodeKind::Dead;
        }
    }
    return live;
}

gc_node_ref_t GCGraph::nodeRef(const GCNode *nodePtr) const {
    for (gc_cnt_t c = 0; c < nativePayload_.chunkCount; ++c) {
        const GCNodeChunk &chunk = *nativePayload_.chunks[c];
        const GCNode *base       = chunk.nodes();
        if (nodePtr >= base && nodePtr < base + chunk.nodeCount) {
            return static_cast<gc_node_ref_t>(c * kNodeChunkNodes + (nodePtr - base));
        }
    }
    return kInvalidNodeRef;
}

std::span<const std::byte> GCGraph::nodePayloadBytes(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (n == nullptr || !n->hasPayload()) {
        return {};
    }
    // The size follows from the kind (and for a FILL from its slot count), as the encoder
    // laid it out (draftPayloadWords): no scan for the next payload.
    const auto *body = reinterpret_cast<const std::byte *>(chunkOf(ref).payload + n->payload);
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

uint64_t GCGraph::nodeContentHash(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (n == nullptr) {
        return 0;
    }
    // FNV-1a over the node: its record, the run of nodes it consumes and its payload (see the
    // declaration in graph.h).
    uint64_t hash = 0xcbf29ce484222325ULL;
    const auto mix = [&hash](const void *data, size_t bytes) {
        const auto *read = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < bytes; ++i) {
            hash ^= read[i];
            hash *= 0x100000001b3ULL;
        }
    };
    const auto mixValue = [&mix](const auto &value) { mix(&value, sizeof(value)); };
    const auto mixInputs = [&mix, &mixValue](std::span<const gc_node_ref_t> inputs) {
        mixValue(static_cast<uint64_t>(inputs.size()));
        for (gc_node_ref_t input : inputs) {
            mixValue(input);
        }
    };
    mixValue(n->kind);
    mixValue(n->flags);
    mixValue(n->reserved);
    mixValue(n->dataIndex);
    // The type enters as its canonical id: two records that name one structure, however their
    // keys spell the names in it, are the same content (camel/core/type/intern.h).
    mixValue(camel::core::type::TypeTable::instance().canonicalIdOf(n->typeId));
    mixInputs(normInputsOf(ref));
    mixInputs(withInputsOf(ref));
    mixInputs(ctrlInputsOf(ref));
    // The order predecessors: part of what the node is, so two nodes that differ only in their
    // resource order are not the same node (they live in the graph's own array, not in the run).
    const auto orderEdges = orderPredsOf(ref);
    mixValue(static_cast<uint64_t>(orderEdges.size()));
    for (const GCGraphOrderEdge &edge : orderEdges) {
        mixValue(edge.pred);
    }
    // The sync predecessor: the statement this one is written after, which is part of what the
    // node is, exactly as the order predecessors are.
    const auto syncEdges = syncPredsOf(ref);
    mixValue(static_cast<uint64_t>(syncEdges.size()));
    for (const GCGraphSyncEdge &edge : syncEdges) {
        mixValue(edge.pred);
    }
    const std::span<const std::byte> payload = nodePayloadBytes(ref);
    mixValue(static_cast<uint64_t>(payload.size()));
    mix(payload.data(), payload.size());
    return hash;
}

GraphId GCGraph::calleeIdOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Func || !n->hasPayload()) {
        return kInvalidGraphId;
    }
    const uint32_t index = nodeBodyAs<GCFuncRef>(ref)->ref;
    return index < refCount_ ? refs_[index] : kInvalidGraphId;
}

GraphId GCGraph::loopCondIdOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Loop || !n->hasPayload()) {
        return kInvalidGraphId;
    }
    const uint32_t index = nodeBodyAs<GCLoopRef>(ref)->condRef;
    return index < refCount_ ? refs_[index] : kInvalidGraphId;
}

GraphId GCGraph::loopBodyIdOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Loop || !n->hasPayload()) {
        return kInvalidGraphId;
    }
    const uint32_t index = nodeBodyAs<GCLoopRef>(ref)->bodyRef;
    return index < refCount_ ? refs_[index] : kInvalidGraphId;
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
    //
    // The fourth group is the sync edges, which are not in the input table: they are a side array
    // sorted by (user, pred), so they are read in the same user order and placed the same way
    // (docs/design/effects/edges.cn.md 1.5).
    const size_t count  = graph.nodeCount();
    const auto groupsOf = [&](gc_node_ref_t user) {
        return std::array<std::span<const gc_node_ref_t>, 3>{
            graph.normInputsOf(user),
            graph.withInputsOf(user),
            graph.ctrlInputsOf(user)};
    };
    // A user reading one source twice in one group is listed once: it is skipped when the
    // previous entry placed for that (source, group) is the same user.
    std::vector<uint32_t> sizes(count * kGroups, 0);
    std::vector<gc_node_ref_t> lastUser(count * kGroups, kInvalidNodeRef);
    for (gc_node_ref_t user = 0; user < count; ++user) {
        const auto groups = groupsOf(user);
        for (size_t kind = 0; kind < 3; ++kind) {
            for (gc_node_ref_t input : groups[kind]) {
                const size_t at = static_cast<size_t>(input) * kGroups + kind;
                if (lastUser[at] != user) {
                    lastUser[at] = user;
                    ++sizes[at];
                }
            }
        }
    }
    for (const GCGraphSyncEdge &edge : graph.syncEdges()) {
        if (!graph.containsNodeRef(edge.pred) || !graph.containsNodeRef(edge.user)) {
            continue;
        }
        const size_t at = static_cast<size_t>(edge.pred) * kGroups + 3;
        if (lastUser[at] != edge.user) {
            lastUser[at] = edge.user;
            ++sizes[at];
        }
    }
    begins_.resize(count * kGroups + 1);
    uint32_t total = 0;
    for (size_t at = 0; at < count * kGroups; ++at) {
        begins_[at] = total;
        total += sizes[at];
    }
    begins_[count * kGroups] = total;
    users_.resize(total);
    std::vector<uint32_t> cursor(begins_.begin(), begins_.end() - 1);
    std::fill(lastUser.begin(), lastUser.end(), kInvalidNodeRef);
    for (gc_node_ref_t user = 0; user < count; ++user) {
        const auto groups = groupsOf(user);
        for (size_t kind = 0; kind < 3; ++kind) {
            for (gc_node_ref_t input : groups[kind]) {
                const size_t at = static_cast<size_t>(input) * kGroups + kind;
                if (lastUser[at] != user) {
                    lastUser[at]         = user;
                    users_[cursor[at]++] = user;
                }
            }
        }
    }
    for (const GCGraphSyncEdge &edge : graph.syncEdges()) {
        if (!graph.containsNodeRef(edge.pred) || !graph.containsNodeRef(edge.user)) {
            continue;
        }
        const size_t at = static_cast<size_t>(edge.pred) * kGroups + 3;
        if (lastUser[at] != edge.user) {
            lastUser[at]         = edge.user;
            users_[cursor[at]++] = edge.user;
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

const ExecPlan &GCGraph::execPlan() const {
    if (ExecPlan *known = execPlan_.load(std::memory_order_acquire)) {
        return *known;
    }
    if (!executable()) [[unlikely]] {
        throw std::logic_error(
            "Graph '" + name() + "' has values with several writings: it has no execution plan "
            "until they are extracted.");
    }
    ExecPlan *built    = buildExecPlan(*this).release();
    ExecPlan *expected = nullptr;
    if (!execPlan_.compare_exchange_strong(
            expected,
            built,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        delete built; // another thread was first
        return *expected;
    }
    return *built;
}

GCGraph::~GCGraph() {
    delete uses_.load(std::memory_order_acquire);
    delete execPlan_.load(std::memory_order_acquire);
}

void GCGraph::releaseNodeChunks() {
    for (gc_cnt_t c = 0; c < nativePayload_.chunkCount; ++c) {
        releaseNodeChunk(nativePayload_.chunks[c]);
    }
}

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

gc_cnt_t GCGraph::armCountOf(gc_node_ref_t brchRef) const {
    const GCNode *n = node(brchRef);
    return n && n->kind == GCNodeKind::Brch ? nodeBodyAs<GCBrchBody>(brchRef)->armCount : 0;
}

bool GCGraph::equals(
    const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
    bool deep) const {
    (void)type;
    (void)deep;
    auto *otherGraph = dynamic_cast<const GCGraph *>(other);
    return otherGraph != nullptr && otherGraph->nativePayload_.chunks == nativePayload_.chunks &&
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
        id_,
        GCGraphDerivation{typeSpecializationRoot_, flags_},
        debug_,
        funcType_,
        runtimeDataType_,
        staticDataType_,
        closureType_,
        outerGraphId_,
        refs_,
        refRoles_,
        refCount_,
        nativePayload_,
        staticArea_);
    graph->revision_ = revision_; // the same content: the same revision
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
}

void collectStaticGraphRefsRecursive(
    std::vector<GraphId> &refs, slot_t slot, Type *type,
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
        const GraphId target = funcObj->graphId();
        ASSERT(isValid(target), "Runtime static graph references must name a graph.");
        pushUnique(refs, target);

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

