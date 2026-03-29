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
 * Updated: Mar. 29, 2026
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
// GraphDraft：一次 rewrite 的工作态。
//
// GraphDraft 持有 source root 的 cloneGraph 工作副本（draft-owned），并负责：
//   1. 默认只把 root 视为 owned graph；子图/依赖图必须显式 import/clone。
//   2. 约束所有编辑只能发生在 draft-owned graph 上（assertDraftOwned）。
//   3. 编辑结束后由 seal() 一次性校验并封印整棵图树。
//   4. 为 GraphRewriteSession 提供 graph-in / graph-out 的内部编辑原语。
// =============================================================================
class GraphDraft {
  public:
    explicit GraphDraft(const graph_ptr_t &source);

    graph_ptr_t sourceRoot() const { return sourceRoot_; }
    graph_ptr_t root() const { return root_; }
    graph_ptr_t canonicalGraph(const graph_ptr_t &graph) const;
    bool hasDraftFor(const Graph *graph) const;
    bool owns(const Graph *graph) const;

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

    void validate() const;
    void seal();

  private:
    void registerOwnedGraphs(const graph_ptr_t &graph);
    graph_ptr_t resolveImportTarget(const graph_ptr_t &graph, GraphImportMode mode);
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
};

} // namespace camel::compile::gir
