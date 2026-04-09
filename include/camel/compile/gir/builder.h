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
 * Created: Mar. 14, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "nodes.h"

namespace camel::compile::gir {

using type_vec_t = camel::core::type::type_vec_t;

enum class InlineSyncPolicy {
    Auto,
    Force,
    Never,
};

struct InlineOptions {
    InlineSyncPolicy syncPolicy = InlineSyncPolicy::Auto;
    bool importReferencedGraphs = true;
};

struct InlineResult {
    Node *callNode  = nullptr;
    Node *valueExit = nullptr;
    Node *ctrlEntry = nullptr;
    std::vector<graph_ptr_t> importedSubgraphs;
    std::vector<graph_ptr_t> importedDependencies;
    bool insertedEntrySync = false;

    explicit operator bool() const { return valueExit != nullptr; }
};

// =============================================================================
// LayoutResult: pure compute output from computeLayout().
//
// Contains the new slot index mapping for all nodes, the static data array,
// and the various signature types. applyLayout() writes it into Graph in one
// pass. Decoupling layout computation from Graph mutation lets sealGraph /
// exportRoot-style flows compute first and apply later.
// =============================================================================
struct LayoutResult {
    std::vector<std::pair<Node *, data_idx_t>> nodeIndices;
    static_slot_vec_t staticDataArr;
    TupleType *staticDataType  = nullptr;
    TupleType *runtimeDataType = nullptr;
    TupleType *closureType     = nullptr;
    size_t runtimeDataSize     = 1;
};

// =============================================================================
// GraphBuilder: the only low-level construction/transformation entry point for Graph.
//
// Responsibilities:
//   1. Create new graphs (createGraph) and clone graphs (cloneGraph).
//   2. Perform single-graph structural edits on draft (non-finalized) graphs:
//      add/remove nodes, ports, closures, subgraphs, and dependencies.
//   3. Seal graphs (sealGraph): run layout computation once (rearrange + finalized
//      frame layout), then freeze all nodes' draft adjacency vectors into arena-backed
//      fixed arrays. After sealing, the graph can no longer be edited.
//
// Design constraints:
//   - GraphBuilder only handles low-level mutation for the current graph_; it does
//     not manage cross-graph transactions.
//   - Mutable build-time state lives in GraphBuilderState (defined in graph.h).
//   - Graph itself exposes no mutable API; all writes go through GraphBuilder (friend).
//   - sealGraph is a one-way terminal step. To modify a sealed graph, cloneGraph it
//     into a draft copy first.
//   - computeLayout() / applyLayout() separate layout computation from mutation.
// =============================================================================
class GraphBuilder {
  public:
    explicit GraphBuilder(Graph &graph) : graph_(&graph) {}
    explicit GraphBuilder(Graph *graph) : graph_(graph) {
        ASSERT(graph_ != nullptr, "Graph is null.");
    }
    explicit GraphBuilder(const graph_ptr_t &graph) : GraphBuilder(graph.get()) {}

    static graph_ptr_t createGraph(
        FunctionType *funcType, const graph_ptr_t &outer = nullptr, const std::string &name = "");
    static graph_ptr_t cloneGraph(
        const graph_ptr_t &graph, std::unordered_map<const Node *, Node *> *nodeMapOut = nullptr);

    Graph &graph() const { return *graph_; }
    graph_ptr_t graphPtr() const { return graph().shared_from_this(); }

    data_idx_t addStaticSlot(slot_t slot) const;
    data_idx_t addStaticData(const data_ptr_t &data) const;
    data_idx_t addRuntimeData() const;
    void setStaticSlot(data_idx_t index, slot_t slot) const;
    void setStaticData(data_idx_t index, const data_ptr_t &data) const;

    Node *ownNode(Node *node) const;
    void addNode(Node *node) const;
    void eraseNode(Node *node) const;

    void addPort(Node *node, bool isWith = false) const;
    void addClosure(Node *node) const;
    void parametrizeClosure() const;
    void setOutput(Node *node) const;

    void addSubGraph(const graph_ptr_t &subGraph) const;
    void eraseSubGraph(const graph_ptr_t &subGraph) const;
    void addDependency(const graph_ptr_t &dep) const;
    void eraseDependency(const graph_ptr_t &dep) const;

    void touch() const {
        assertBuildable("mark dirty on");
        markMutated();
    }
    InlineResult inlineCallable(Node *node, const InlineOptions &options = {}) const;
    void pruneUnreachable() const;
    /// Seal this graph: run layout computation (rearrange + finalized frame layout),
    /// then move all nodes' draft adjacency vectors into arena-backed fixed arrays.
    /// This has consume semantics: after the call, the builder is no longer usable
    /// (graph_ becomes nullptr).
    void sealGraph();
    /// Recursively seal the whole graph tree (depth-first).
    static void sealGraphRecursively(const graph_ptr_t &graph);

    /// Pure computation: walk graph ports/closure/nodes, generate slot mappings
    /// and signature types, and modify no Graph members. applyLayout() writes the
    /// result later in one pass.
    static LayoutResult computeLayout(const Graph &graph);

    static void validateGraph(const Graph &graph);
    static void validateGraphRecursively(const graph_ptr_t &graph);

  private:
    // All writes go to GraphBuilderState (staging) first, then syncStateToGraph
    // mirrors them into Graph fields. After sealing, staging is consumed and
    // Graph returns to a read-only view.
    /// Internal seal state machine: Draft -> Sealing -> Sealed (idempotent).
    /// Fixed order: rearrange -> debug promote -> freeze adjacency -> static pack
    /// -> installFinalFrameLayout -> releaseDraftRegion.
    void finalize() const;
    /// Write the result of computeLayout() into graph: update each node's dataIndex
    /// and update Graph's signature / staticDataArr / finalized frame layout.
    static void applyLayout(Graph &graph, const LayoutResult &layout);
    void rearrange() const;
    void assertBuildable(const char *action) const;
    void markMutated() const;
    GraphBuilderState &state() const;
    static std::shared_ptr<GraphBuilderState> snapshotStateFromGraph(Graph &graph);
    void syncStateToGraph() const;

    Graph *graph_;
};

// GraphDraft has been split into draft.h to carry owned scopes, source->draft
// normalization, and sealing export for cross-graph rewrite sessions.
// builder.h keeps only the single-graph low-level mutation API.

} // namespace camel::compile::gir
