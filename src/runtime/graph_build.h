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
 * Created: Apr. 11, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Internal GCGraph construction helpers shared by runtime graph materializers.
 * This file is intentionally not exposed as a public runtime API.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

class GraphDraft;

/// The graph's cold data: its names, the names it captures and how it was derived.
struct GCGraphDebugRecord {
    std::string mangledName;
    std::string name;
    // GCGraph::typeSpecializationRoot and GCGraph::flags.
    GraphId typeSpecializationRoot = kInvalidGraphId;
    uint8_t flags                  = 0;
    // The names the graph captures, in the order it captured them (GCGraph::captureNames).
    std::vector<camel::core::SymId> captureNames;
};

struct GCGraphPayloadShape {
    gc_cnt_t nodeCount        = 0;
    gc_cnt_t edgeCount        = 0;
    gc_cnt_t payloadWordCount = 0;
    gc_cnt_t normPortCount    = 0;
    gc_cnt_t withPortCount    = 0;
    gc_cnt_t closureCount     = 0;
    gc_cnt_t branchArmCount   = 0;
    // Whether some node has numeric permissions: only then the graph has a permission column.
    bool permissions = false;
    // Whether some node has attributes: only then the graph has an attribute column.
    bool attrs = false;
};

struct GCGraphPayloadArena {
    // The graph's reference table, already built, and the index of each entry: the encoder
    // writes a FUNC node's callee as its index (GCFuncRef).
    std::span<GCGraph *const> refs;
    const std::unordered_map<const GCGraph *, uint32_t> *refIndex = nullptr;

    GCNode *nodes              = nullptr; // nodeCount + 1 records
    uint32_t *origins          = nullptr; // nodeCount source origins
    uint8_t *permissions       = nullptr; // nodeCount numeric permissions, or null
    uint32_t *attrs            = nullptr; // nodeCount attribute sets, or null
    gc_node_ref_t *inputs      = nullptr;
    gc_payload_word_t *payload = nullptr;
    gc_node_ref_t *normPorts   = nullptr;
    gc_node_ref_t *withPorts   = nullptr;
    gc_node_ref_t *closure     = nullptr;
    GCBranchArm *branchArms    = nullptr;

    gc_cnt_t nodeCount        = 0;
    gc_cnt_t edgeCount        = 0;
    gc_cnt_t payloadWordCount = 0;
    gc_cnt_t normPortCount    = 0;
    gc_cnt_t withPortCount    = 0;
    gc_cnt_t closureCount     = 0;
    gc_cnt_t branchArmCount   = 0;

    gc_node_ref_t entryNode  = kInvalidNodeRef;
    gc_node_ref_t exitNode   = kInvalidNodeRef;
    gc_node_ref_t outputNode = kInvalidNodeRef;
    gc_node_ref_t returnNode = kInvalidNodeRef;
    GCReturnKind returnKind  = GCReturnKind::None;
};

GCGraphDebugRecord *createGraphDebugRecord(std::string mangledName, std::string name);

/// Records the cold data of `draft` in `record`: the names it captures and how it was derived.
/// (The source origins of its nodes are a column of the frozen graph, written with the records.)
/// Every encoding of a draft calls it, so that nothing of the draft's debug data is lost on a
/// commit.
void recordDraftDebugData(GCGraphDebugRecord *record, const GraphDraft &draft);

/// The frame layout of a graph being built: the slot types, slot 0 included, or nullopt for a
/// graph without one. The encoder builds the layout inside the graph's block
/// (TupleType::constructFrameLayout).
using FrameSlotTypes = std::optional<std::span<camel::core::type::Type *const>>;

struct GCGraphBuildAccess {
    static size_t requiredBytes(
        std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
        std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payload,
        std::span<const slot_t> staticSlots, const FrameSlotTypes &frameSlots);

    static GCGraph *constructInPlace(
        void *memory, size_t bytes, GraphId id, GCGraphDebugRecord *debugRecord,
        camel::core::type::FunctionType *funcType, const FrameSlotTypes &frameSlots,
        camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
        GCGraph *outerGraph, std::span<GCGraph *const> dependencies,
        std::span<GCGraph *const> subGraphs, std::span<GCGraph *const> staticGraphRefs,
        const GCGraphPayloadShape &payloadShape,
        const std::function<void(GCGraphPayloadArena &)> &emitPayload,
        std::span<const slot_t> staticSlots);

    static GCGraph *create(
        GraphId id, GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
        const FrameSlotTypes &frameSlots, camel::core::type::TupleType *staticDataType,
        camel::core::type::TupleType *closureType, GCGraph *outerGraph,
        std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
        std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payloadShape,
        const std::function<void(GCGraphPayloadArena &)> &emitPayload,
        std::span<const slot_t> staticSlots);

    /// Makes `copy`, a commit's encoding of `source`, have the revision of `source` when the
    /// commit did not change it (GCGraph::revision). Set before anything can see `copy`.
    static void inheritRevision(GCGraph *copy, const GCGraph *source, bool changed) {
        if (!changed) {
            copy->revision_ = source->revision_;
        }
    }
};
GCGraphPayloadShape describeDraftNativePayload(const GraphDraft &draft);
void emitDraftNativePayload(
    const GraphDraft &draft, GCGraphPayloadArena &payload,
    const std::function<GCGraph *(GCGraph *)> &resolveRuntimeGraph = {});

void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, camel::core::type::Type *type,
    std::unordered_set<const camel::core::rtdata::Object *> &visited);
/// Structural checks on one encoded graph, in every build mode: node, slot and edge references
/// in range, inputs and outputs mirroring each other, BRCH/JOIN arm counts, GATE with a value
/// and a control input, FUNC with a callee, CALL with a callee input. Throws std::runtime_error;
/// inside a pass that becomes RuntimeDiag::PassInternalError naming the pass.
void validateRuntimeGraphPayload(const GCGraph *graph);
/// The branch-arm invariant (branch_arms.cpp; docs/technical/17_graph_rewrite_api_contracts.md,
/// 4.1): between a BRCH and its JOIN every executed node belongs to exactly one arm. Part of
/// validateRuntimeGraphPayload; throws std::runtime_error naming the graph, the node and the arms.
void validateRuntimeBranchArms(const GCGraph *graph);
/// The type invariant (graph_types.cpp): no type on the graph (function type, runtime, static
/// and closure slots, at any depth) is an auxiliary type of the compiler. Part of
/// validateRuntimeGraphPayload; throws std::runtime_error naming the graph and the type.
void validateRuntimeGraphTypes(const GCGraph *graph);
/// The checks that need the callees encoded: every direct call passes as many arguments as its
/// callee has ports, and every graph a function constant of the static area points to is in
/// the reference table (graphs refer to graphs only through it; the FUNC payload is an index
/// into it by construction, checked by validateRuntimeGraphPayload). Run on each graph once the
/// whole set of a commit is encoded.
void validateRuntimeGraphCalls(const GCGraph *graph);

} // namespace camel::runtime
