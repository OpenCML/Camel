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
 * Created: Mar. 29, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "builder.h"

#include <unordered_map>
#include <unordered_set>

namespace camel::compile::gir {

enum class GraphImportMode {
    ReferenceOnly,
    CloneIntoDraft,
};

// =============================================================================
// GraphDraft: working state for one rewrite pass.
//
// GraphDraft holds a draft-owned clone of the source root and is responsible for:
//   1. Treating only the root as owned by default; subgraphs/dependencies must
//      be explicitly imported or cloned.
//   2. Restricting edits to draft-owned graphs only (assertDraftOwned).
//   3. Validating and sealing the entire graph tree once editing is done.
//   4. Providing GraphRewriteSession with internal graph-in / graph-out edit primitives.
// =============================================================================
class GraphDraft {
  public:
    explicit GraphDraft(const graph_ptr_t &source);

    graph_ptr_t sourceRoot() const { return sourceRoot_; }
    graph_ptr_t root() const { return root_; }
    graph_ptr_t canonicalGraph(const graph_ptr_t &graph) const;
    Node *canonicalNode(const Node *node) const;
    bool hasDraftFor(const Graph *graph) const;
    bool hasDraftFor(const Node *node) const;
    bool owns(const Graph *graph) const;
    void adoptOwnedGraph(const graph_ptr_t &graph);

    graph_ptr_t cloneIntoDraft(const graph_ptr_t &graph);
    graph_ptr_t importSubGraph(
        const graph_ptr_t &owner, const graph_ptr_t &subGraph,
        GraphImportMode mode = GraphImportMode::ReferenceOnly);
    graph_ptr_t importDependency(
        const graph_ptr_t &owner, const graph_ptr_t &dep,
        GraphImportMode mode = GraphImportMode::ReferenceOnly);

    void eraseNode(Node *node);
    bool replaceNode(Node *oldNode, Node *newNode);
    bool replaceAllUses(Node *oldNode, Node *newNode);

    void addDependency(const graph_ptr_t &owner, const graph_ptr_t &dep);
    void eraseDependency(const graph_ptr_t &owner, const graph_ptr_t &dep);
    void retargetDependency(
        const graph_ptr_t &owner, const graph_ptr_t &oldDep, const graph_ptr_t &newDep,
        GraphImportMode mode = GraphImportMode::ReferenceOnly);
    void eraseSubGraph(const graph_ptr_t &owner, const graph_ptr_t &subGraph);

    void link(LinkType type, Node *from, Node *to);
    void replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput);
    void replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput);

    InlineResult inlineCallable(Node *node, const InlineOptions &options = {});
    void pruneUnreachable(const graph_ptr_t &graph);
    void normalizeAllOwnedStaticValues();

    void validate() const;
    void seal();

  private:
    void registerOwnedGraph(const graph_ptr_t &sourceGraph, const graph_ptr_t &draftGraph);
    void registerNodeMap(const std::unordered_map<const Node *, Node *> &nodeMap);
    graph_ptr_t resolveImportTarget(const graph_ptr_t &graph, GraphImportMode mode);
    graph_ptr_t sourceGraphOfDraft(const Graph *draftGraph) const;
    void normalizeOwnedStaticValues(Graph *ownerRaw);
    void retargetOwnedGraphRefs(const graph_ptr_t &sourceGraph, const graph_ptr_t &draftGraph);
    void retargetKnownDraftRefsInOwner(Graph *ownerRaw);
    static void ensureDependencyRegistration(
        Graph *ownerRaw, const graph_ptr_t &dep, GraphBuilder &ownerBuilder);
    void assertDraftOwned(const Graph *graph, const char *action) const;
    void markDirty(Graph *graph);

    graph_ptr_t sourceRoot_;
    graph_ptr_t root_;
    std::unordered_set<Graph *> ownedGraphs_;
    std::unordered_set<Graph *> dirtyGraphs_;
    std::unordered_map<const Graph *, graph_ptr_t> draftBySource_;
    std::unordered_map<const Graph *, graph_ptr_t> sourceByDraft_;
    std::unordered_map<const Node *, Node *> draftNodeBySource_;
};

} // namespace camel::compile::gir
