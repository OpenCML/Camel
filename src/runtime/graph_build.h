// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
class Program;

/// The graph's cold data: its names and the names it captures.
struct GCGraphDebugRecord {
    std::string mangledName;
    std::string name;
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
    // Whether some node has numeric permissions: only then the graph has a permission column.
    bool permissions = false;
    // Whether some node has attributes: only then the graph has an attribute column.
    bool attrs = false;
    // Whether some node is in a region but the body: only then the graph has a region column.
    bool regions = false;
    gc_cnt_t regionCount     = 0;
    gc_cnt_t regionPortCount = 0;
    // The equivalences between the nodes (GCGraph::writings); only then the graph is not in the
    // executable state.
    gc_cnt_t writingCount = 0;
    // The graph the draft was decoded from: a chunk of the new graph that has all the content of
    // the same chunk of this one is that chunk (GCNodeChunk::shares). nullptr: none.
    const GCGraph *base = nullptr;
};

struct GCGraphPayloadArena {
    // The graph's reference table, already built, and the index of each entry: the encoder
    // writes a FUNC node's callee as its index (GCFuncRef).
    std::span<const GraphId> refs;
    const std::unordered_map<GraphId, uint32_t> *refIndex = nullptr;

    GCNode *nodes              = nullptr; // nodeCount + 1 records
    uint32_t *origins          = nullptr; // nodeCount source origins
    camel::core::sem::PermissionMask *permissions = nullptr; // nodeCount permissions, or null
    uint32_t *attrs            = nullptr; // nodeCount attribute sets, or null
    uint32_t *regions          = nullptr; // nodeCount regions, or null
    GCRegion *regionTable      = nullptr; // regionCount regions
    gc_node_ref_t *regionPorts = nullptr; // regionPortCount form ports
    gc_cnt_t regionCount       = 0;
    gc_cnt_t regionPortCount   = 0;
    WritingUnion *writings     = nullptr; // writingCount equivalences
    gc_cnt_t writingCount      = 0;
    gc_node_ref_t *inputs      = nullptr;
    gc_payload_word_t *payload = nullptr;
    gc_node_ref_t *normPorts   = nullptr;
    gc_node_ref_t *withPorts   = nullptr;
    gc_node_ref_t *closure     = nullptr;

    gc_cnt_t nodeCount        = 0;
    gc_cnt_t edgeCount        = 0;
    gc_cnt_t payloadWordCount = 0;
    gc_cnt_t normPortCount    = 0;
    gc_cnt_t withPortCount    = 0;
    gc_cnt_t closureCount     = 0;

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

/// How a graph was derived, besides what its nodes say (GCGraph::typeSpecializationRoot,
/// GCGraph::flags).
struct GCGraphDerivation {
    GraphId typeSpecializationRoot = kInvalidGraphId;
    uint8_t flags                  = 0;
};

/// The frame layout of a graph being built: the slot types, slot 0 included, or nullopt for a
/// graph without one. The encoder builds the layout inside the graph's block
/// (TupleType::constructFrameLayout).
using FrameSlotTypes = std::optional<std::span<camel::core::type::Type *const>>;

struct GCGraphBuildAccess {
    static size_t requiredBytes(
        std::span<const GraphId> dependencies, std::span<const GraphId> subGraphs,
        std::span<const GraphId> staticGraphRefs, std::span<const GraphId> ruleGraphs,
        const GCGraphPayloadShape &payload, std::span<const slot_t> staticSlots,
        const FrameSlotTypes &frameSlots);

    static GCGraph *constructInPlace(
        void *memory, size_t bytes, GraphId id, const GCGraphDerivation &derivation,
        GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
        const FrameSlotTypes &frameSlots, camel::core::type::TupleType *staticDataType,
        camel::core::type::TupleType *closureType, GraphId outerGraph,
        std::span<const GraphId> dependencies, std::span<const GraphId> subGraphs,
        std::span<const GraphId> staticGraphRefs, std::span<const GraphId> ruleGraphs,
        const GCGraphPayloadShape &payloadShape,
        const std::function<void(GCGraphPayloadArena &)> &emitPayload,
        std::span<const slot_t> staticSlots);

    static GCGraph *create(
        GraphId id, const GCGraphDerivation &derivation, GCGraphDebugRecord *debugRecord,
        camel::core::type::FunctionType *funcType, const FrameSlotTypes &frameSlots,
        camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
        GraphId outerGraph, std::span<const GraphId> dependencies,
        std::span<const GraphId> subGraphs, std::span<const GraphId> staticGraphRefs,
        std::span<const GraphId> ruleGraphs, const GCGraphPayloadShape &payloadShape,
        const std::function<void(GCGraphPayloadArena &)> &emitPayload,
        std::span<const slot_t> staticSlots);

    /// Makes `copy`, a commit's encoding of `source`, have the revision of `source` when the
    /// commit did not change it (GCGraph::revision). Set before anything can see `copy`.
    static void addFlags(GCGraph *graph, uint8_t flags) { graph->flags_ |= flags; }
    static void inheritRevision(GCGraph *copy, const GCGraph *source, bool changed) {
        if (!changed) {
            copy->revision_ = source->revision_;
        }
    }
};
GCGraphPayloadShape describeDraftNativePayload(const GraphDraft &draft);
void emitDraftNativePayload(const GraphDraft &draft, GCGraphPayloadArena &payload);

void collectStaticGraphRefsRecursive(
    std::vector<GraphId> &refs, slot_t slot, camel::core::type::Type *type,
    std::unordered_set<const camel::core::rtdata::Object *> &visited);
/// Structural checks on one encoded graph, in every build mode: node, slot and edge references
/// in range, inputs and outputs mirroring each other, BRCH/JOIN arm counts, GATE with a value
/// and a control input, FUNC with a callee, CALL with a callee input. Throws std::runtime_error;
/// inside a pass that becomes RuntimeDiag::PassInternalError naming the pass.
void validateRuntimeGraphPayload(const Program &program, const GCGraph *graph);
/// The branch-arm invariant (branch_arms.cpp; docs/technical/17_graph_rewrite_api_contracts.md,
/// 4.1): between a BRCH and its JOIN every executed node belongs to exactly one arm. Part of
/// validateRuntimeGraphPayload; throws std::runtime_error naming the graph, the node and the arms.
void validateRuntimeBranchArms(const Program &program, const GCGraph *graph);
/// The type invariant (graph_types.cpp): no type on the graph (function type, runtime, static
/// and closure slots, at any depth) is an auxiliary type of the compiler. Part of
/// validateRuntimeGraphPayload; throws std::runtime_error naming the graph and the type.
void validateRuntimeGraphTypes(const GCGraph *graph);
/// The checks that need the callees encoded: every direct call passes as many arguments as its
/// callee has ports, and every graph a function constant of the static area points to is in
/// the reference table (graphs refer to graphs only through it; the FUNC payload is an index
/// into it by construction, checked by validateRuntimeGraphPayload). Run on each graph once the
/// whole set of a commit is encoded. `state` is the graph's (GraphState): the state-dependent
/// rules are applied here, with the edge invariants, so that every state is checked by this one
/// validator.
void validateRuntimeGraphCalls(
    const Program &program, const GCGraph *graph, GraphState state = GraphState::Executable);
/// The edge invariants (graph_edges.cpp): no cycle of inputs, and the types agree along the
/// edges. Run with validateRuntimeGraphCalls, because the types of a call's callee are read.
/// Throws std::runtime_error naming the graph, the nodes and what disagrees.
void validateRuntimeGraphEdges(
    const Program &program, const GCGraph *graph, GraphState state = GraphState::Executable);

} // namespace camel::runtime

