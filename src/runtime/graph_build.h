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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Internal GCGraph construction helpers shared by runtime graph materializers.
 * This file is intentionally not exposed as a public runtime API.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

class GraphDraft;

struct GCGraphDebugRecord {
    std::string stableId;
    std::string mangledName;
    std::string name;
};

struct GCGraphPayloadShape {
    gc_cnt_t nodeCount      = 0;
    gc_cnt_t nodeBlockCount = 0;
    gc_cnt_t edgeCount      = 0;
    gc_cnt_t normPortCount  = 0;
    gc_cnt_t withPortCount  = 0;
    gc_cnt_t closureCount   = 0;
    gc_cnt_t branchArmCount = 0;
};

struct GCGraphPayloadArena {
    gc_block_t *nodeBlocks   = nullptr;
    gc_node_ref_t *edges     = nullptr;
    gc_node_ref_t *normPorts = nullptr;
    gc_node_ref_t *withPorts = nullptr;
    gc_node_ref_t *closure   = nullptr;
    GCBranchArm *branchArms  = nullptr;

    gc_cnt_t nodeCount      = 0;
    gc_cnt_t nodeBlockCount = 0;
    gc_cnt_t edgeCount      = 0;
    gc_cnt_t normPortCount  = 0;
    gc_cnt_t withPortCount  = 0;
    gc_cnt_t closureCount   = 0;
    gc_cnt_t branchArmCount = 0;

    gc_node_ref_t entryNode  = kInvalidNodeRef;
    gc_node_ref_t exitNode   = kInvalidNodeRef;
    gc_node_ref_t outputNode = kInvalidNodeRef;
    gc_node_ref_t returnNode = kInvalidNodeRef;
    GCReturnKind returnKind  = GCReturnKind::None;
};

GCGraphDebugRecord *
createGraphDebugRecord(std::string stableId, std::string mangledName, std::string name);

struct GCGraphBuildAccess {
    static size_t requiredBytes(
        std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
        std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payload,
        std::span<const slot_t> staticSlots);

    static GCGraph *constructInPlace(
        void *memory, size_t bytes, GCGraphDebugRecord *debugRecord,
        camel::core::type::FunctionType *funcType, camel::core::type::TupleType *runtimeDataType,
        camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
        GCGraph *outerGraph, std::span<GCGraph *const> dependencies,
        std::span<GCGraph *const> subGraphs, std::span<GCGraph *const> staticGraphRefs,
        const GCGraphPayloadShape &payloadShape,
        const std::function<void(GCGraphPayloadArena &)> &emitPayload,
        std::span<const slot_t> staticSlots);

    static GCGraph *create(
        GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
        camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
        camel::core::type::TupleType *closureType, GCGraph *outerGraph,
        std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
        std::span<GCGraph *const> staticGraphRefs, const GCGraphPayloadShape &payloadShape,
        const std::function<void(GCGraphPayloadArena &)> &emitPayload,
        std::span<const slot_t> staticSlots);
};
GCGraphPayloadShape describeDraftNativePayload(const GraphDraft &draft);
void emitDraftNativePayload(
    const GraphDraft &draft, GCGraphPayloadArena &payload,
    const std::function<GCGraph *(GCGraph *)> &resolveRuntimeGraph = {});

void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, camel::core::type::Type *type,
    std::unordered_set<const camel::core::rtdata::Object *> &visited);
void validateRuntimeGraphPayload(const GCGraph *graph);

} // namespace camel::runtime
