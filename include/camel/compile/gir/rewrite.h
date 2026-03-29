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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "builder.h"

namespace camel::compile::gir {

struct RewriteResult {
    graph_ptr_t graph;
    bool changed;
};

// =============================================================================
// GraphRewriteSession：pass 侧看到的高层 rewrite 接口。
//
// pass 只描述"删什么、替换什么、内联什么"，不直接操心工作图何时重排、
// 何时封印。所有实际编辑都落到内部 GraphDraft 上；若整个 session 未产生改写，
// finish() 会直接返回原始 source graph，避免导出一份只做了浅克隆的伪结果。
//
// 典型用法（在 rewrite pass 的 apply 方法中）：
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
            draft_.seal();
        }
        return RewriteResult{draft_.root(), changed_};
    }

  private:
    GraphDraft draft_;
    bool changed_ = false;
};

} // namespace camel::compile::gir
