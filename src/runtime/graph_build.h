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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Internal GCGraph construction helpers shared by compile-side Graph::encode
 * and runtime-side GraphDraft::encode. This file is intentionally not exposed
 * as a public runtime API.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::compile::gir {
class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace camel::compile::gir

namespace camel::runtime {

class GraphDraft;

struct GCGraphDebugRecord {
    std::string stableId;
    std::string mangledName;
    std::string name;
};

GCGraphDebugRecord *
createGraphDebugRecord(std::string stableId, std::string mangledName, std::string name);

struct GCGraphBuildAccess {
    static GCGraph *create(
        GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
        camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
        camel::core::type::TupleType *closureType, GCGraph *outerGraph,
        std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
        std::span<GCGraph *const> staticGraphRefs, const GCGraphNativePayload &payload,
        std::span<const slot_t> staticSlots);

    static void rewrite(
        GCGraph *graph, camel::core::type::TupleType *staticDataType, GCGraph *outerGraph,
        std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
        std::span<GCGraph *const> staticGraphRefs, std::span<const slot_t> staticSlots,
        const GCGraphNativePayload *payload = nullptr);
};

GCGraphNativePayload buildCompileGraphNativePayload(
    const camel::compile::gir::graph_ptr_t &graph,
    const std::function<GCGraph *(const camel::compile::gir::Graph *)> &resolveRuntimeGraph);
GCGraphNativePayload buildDraftNativePayload(const GraphDraft &draft);

slot_t canonicalizeStaticSlotRecursive(
    slot_t slot, camel::core::type::Type *type,
    std::unordered_map<const camel::compile::gir::Graph *, GCGraph *> &cache,
    std::unordered_map<const camel::core::rtdata::Object *, camel::core::rtdata::Object *>
        &objectCache,
    const camel::compile::gir::Graph *sourceOwner);
void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, camel::core::type::Type *type,
    std::unordered_set<const camel::core::rtdata::Object *> &visited);
void validateRuntimeGraphPayload(const GCGraph *graph);

} // namespace camel::runtime
