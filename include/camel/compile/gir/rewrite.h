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
 * Created: Mar. 12, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "draft.h"

namespace camel::compile::gir {

struct RewriteResult {
    graph_ptr_t graph;
    bool changed;
};

struct StaticValueMaterializationOptions {
    bool propagateMacro = false;
};

// =============================================================================
// GraphRewriteSession: the high-level rewrite interface seen by passes.
//
// A pass only describes what to delete, replace, or inline; it does not manage
// when the working graph is rearranged or sealed. All actual edits go through
// the internal GraphDraft. If the session performs no rewrite at all, finish()
// returns the original source graph directly instead of emitting a shallow clone.
//
// Typical usage (inside a rewrite pass's apply method):
//   GraphRewriteSession session(frozenGraph);
//   for (Node *n : session.root()->nodes()) {
//       if (shouldReplace(n)) session.replaceNode(n, makeNew(n));
//   }
//   return session.finish();  // -> RewriteResult { newSealedGraph, changed }
// =============================================================================
class GraphRewriteSession {
  public:
    explicit GraphRewriteSession(const graph_ptr_t &graph) : draft_(graph) {}

    graph_ptr_t root() const { return draft_.root(); }
    graph_ptr_t sourceRoot() const { return draft_.sourceRoot(); }
    graph_ptr_t ensureDraftGraph(const graph_ptr_t &graph) {
        ASSERT(graph != nullptr, "Cannot ensure null graph in rewrite session.");
        graph_ptr_t draft = draft_.cloneIntoDraft(graph);
        if (draft.get() != graph.get()) {
            changed_ = true;
        }
        return draft;
    }
    std::vector<graph_ptr_t>
    collectReachableSourceGraphs(const graph_ptr_t &graph = nullptr) const {
        const graph_ptr_t &source = graph ? graph : draft_.sourceRoot();
        std::vector<graph_ptr_t> out;
        std::unordered_set<const Graph *> visited;
        collectReachableGraphs(source, out, visited);
        return out;
    }
    std::unordered_map<const Graph *, graph_ptr_t>
    materializeReachableGraphs(const graph_ptr_t &graph = nullptr) {
        const graph_ptr_t &source = graph ? graph : draft_.sourceRoot();
        ASSERT(source != nullptr, "Cannot materialize reachable graphs from null source root.");
        std::unordered_map<const Graph *, graph_ptr_t> cache{
            {source.get(), canonicalGraph(source)}};
        for (const auto &sourceGraph : collectReachableSourceGraphs(source)) {
            (void)materializeEditableGraph(source, sourceGraph, cache);
        }
        draft_.normalizeAllOwnedStaticValues();
        return cache;
    }
    graph_ptr_t canonicalGraph(const graph_ptr_t &graph) const {
        return draft_.canonicalGraph(graph);
    }
    Node *canonicalNode(const Node *node) const { return draft_.canonicalNode(node); }
    bool hasDraftGraph(const Graph *graph) const { return draft_.hasDraftFor(graph); }
    bool ownsGraph(const Graph *graph) const { return draft_.owns(graph); }
    bool hasDraftNode(const Node *node) const { return draft_.hasDraftFor(node); }
    void adoptOwnedGraph(const graph_ptr_t &graph) {
        ASSERT(graph != nullptr, "Cannot adopt null graph into rewrite session.");
        draft_.adoptOwnedGraph(graph);
        changed_ = true;
    }
    Node *materializeStaticValue(
        const graph_ptr_t &owner, slot_t slot, Type *type,
        const StaticValueMaterializationOptions &options = {});

    void markChanged() { changed_ = true; }

    bool replaceNode(Node *oldNode, Node *newNode) {
        ASSERT(oldNode != nullptr && newNode != nullptr, "Cannot replace null node.");
        bool changed = draft_.replaceNode(oldNode, newNode);
        changed_     = changed_ || changed;
        return changed;
    }
    bool replaceAllUses(Node *oldNode, Node *newNode) {
        ASSERT(oldNode != nullptr && newNode != nullptr, "Cannot replace uses of null node.");
        bool changed = draft_.replaceAllUses(oldNode, newNode);
        changed_     = changed_ || changed;
        return changed;
    }

    void eraseNode(Node *node) {
        ASSERT(node != nullptr, "Cannot erase null node.");
        draft_.eraseNode(node);
        changed_ = true;
    }

    graph_ptr_t importSubGraph(
        const graph_ptr_t &owner, const graph_ptr_t &subGraph,
        GraphImportMode mode = GraphImportMode::ReferenceOnly) {
        graph_ptr_t imported = draft_.importSubGraph(owner, subGraph, mode);
        changed_             = true;
        return imported;
    }

    void addDependency(const graph_ptr_t &owner, const graph_ptr_t &dependency) {
        ASSERT(owner != nullptr && dependency != nullptr, "Cannot add null dependency.");
        draft_.addDependency(owner, dependency);
        changed_ = true;
    }
    graph_ptr_t importDependency(
        const graph_ptr_t &owner, const graph_ptr_t &dependency,
        GraphImportMode mode = GraphImportMode::ReferenceOnly) {
        graph_ptr_t imported = draft_.importDependency(owner, dependency, mode);
        changed_             = true;
        return imported;
    }

    void eraseDependency(const graph_ptr_t &owner, const graph_ptr_t &dependency) {
        ASSERT(owner != nullptr && dependency != nullptr, "Cannot erase null dependency.");
        draft_.eraseDependency(owner, dependency);
        changed_ = true;
    }
    void retargetDependency(
        const graph_ptr_t &owner, const graph_ptr_t &oldDependency,
        const graph_ptr_t &newDependency, GraphImportMode mode = GraphImportMode::ReferenceOnly) {
        ASSERT(
            owner != nullptr && oldDependency != nullptr && newDependency != nullptr,
            "Cannot retarget null dependency.");
        draft_.retargetDependency(owner, oldDependency, newDependency, mode);
        changed_ = true;
    }

    void eraseSubGraph(const graph_ptr_t &owner, const graph_ptr_t &subGraph) {
        ASSERT(owner != nullptr && subGraph != nullptr, "Cannot erase null subgraph.");
        draft_.eraseSubGraph(owner, subGraph);
        changed_ = true;
    }

    InlineResult inlineCallable(Node *node, const InlineOptions &options = {}) {
        ASSERT(node != nullptr, "Cannot inline null node.");
        InlineResult result = draft_.inlineCallable(node, options);
        changed_            = changed_ || static_cast<bool>(result);
        return result;
    }

    void link(LinkType type, Node *from, Node *to) {
        draft_.link(type, from, to);
        changed_ = true;
    }

    void replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
        draft_.replaceInput(type, owner, oldInput, newInput);
        changed_ = true;
    }

    void replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
        draft_.replaceOutput(type, owner, oldOutput, newOutput);
        changed_ = true;
    }
    void pruneUnreachable(const graph_ptr_t &graph) {
        draft_.pruneUnreachable(graph);
        changed_ = true;
    }

    RewriteResult finish() {
        if (!changed_) {
            return RewriteResult{draft_.sourceRoot(), false};
        }
        if (draft_.root()) {
            draft_.normalizeAllOwnedStaticValues();
            draft_.seal();
        }
        return RewriteResult{draft_.root(), changed_};
    }

  private:
    static void collectReachableGraphs(
        const graph_ptr_t &graph, std::vector<graph_ptr_t> &out,
        std::unordered_set<const Graph *> &visited);
    static bool hasSubGraphRef(const graph_ptr_t &owner, const graph_ptr_t &candidate) {
        for (const auto &[_, subGraphs] : owner->subGraphs()) {
            if (subGraphs.contains(candidate)) {
                return true;
            }
        }
        return false;
    }
    graph_ptr_t materializeEditableGraph(
        const graph_ptr_t &sourceRoot, const graph_ptr_t &sourceGraph,
        std::unordered_map<const Graph *, graph_ptr_t> &cache) {
        ASSERT(sourceGraph != nullptr, "Cannot materialize null graph into rewrite draft.");
        if (sourceGraph == sourceRoot) {
            graph_ptr_t canonical   = canonicalGraph(sourceRoot);
            cache[sourceRoot.get()] = canonical;
            return canonical;
        }
        if (auto it = cache.find(sourceGraph.get()); it != cache.end()) {
            return it->second;
        }
        if (hasDraftGraph(sourceGraph.get())) {
            graph_ptr_t canonical    = canonicalGraph(sourceGraph);
            cache[sourceGraph.get()] = canonical;
            return canonical;
        }

        graph_ptr_t imported;
        if (auto sourceOwner = sourceGraph->outer()) {
            graph_ptr_t draftOwner = materializeEditableGraph(sourceRoot, sourceOwner, cache);
            if (hasSubGraphRef(draftOwner, sourceGraph)) {
                eraseSubGraph(draftOwner, sourceGraph);
            }
            if (draftOwner->dependencies().contains(sourceGraph)) {
                eraseDependency(draftOwner, sourceGraph);
            }
            imported = importSubGraph(draftOwner, sourceGraph, GraphImportMode::CloneIntoDraft);
            if (sourceOwner->dependencies().contains(sourceGraph)) {
                importDependency(draftOwner, imported);
            }
        } else {
            graph_ptr_t draftRoot = materializeEditableGraph(sourceRoot, sourceRoot, cache);
            if (draftRoot->dependencies().contains(sourceGraph)) {
                eraseDependency(draftRoot, sourceGraph);
            }
            imported = importDependency(draftRoot, sourceGraph, GraphImportMode::CloneIntoDraft);
        }

        graph_ptr_t canonical    = canonicalGraph(imported);
        cache[sourceGraph.get()] = canonical;
        return canonical;
    }

    GraphDraft draft_;
    bool changed_ = false;
};

} // namespace camel::compile::gir
