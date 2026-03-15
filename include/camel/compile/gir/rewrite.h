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
 * Updated: Mar. 15, 2026
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
// 何时封印。所有实际编辑都落到内部 GraphDraft 上，finish() 统一封印并导出新图。
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

    void eraseNode(Node *node) {
        ASSERT(node != nullptr, "Cannot erase null node.");
        draft_.eraseNode(node);
        changed_ = true;
    }

    void addDependency(const graph_ptr_t &owner, const graph_ptr_t &dependency) {
        ASSERT(owner != nullptr && dependency != nullptr, "Cannot add null dependency.");
        draft_.addDependency(owner, dependency);
        changed_ = true;
    }

    void eraseDependency(const graph_ptr_t &owner, const graph_ptr_t &dependency) {
        ASSERT(owner != nullptr && dependency != nullptr, "Cannot erase null dependency.");
        draft_.eraseDependency(owner, dependency);
        changed_ = true;
    }

    void eraseSubGraph(const graph_ptr_t &owner, const graph_ptr_t &subGraph) {
        ASSERT(owner != nullptr && subGraph != nullptr, "Cannot erase null subgraph.");
        draft_.eraseSubGraph(owner, subGraph);
        changed_ = true;
    }

    Node *inlineNode(Node *node, bool forceSync = false) {
        ASSERT(node != nullptr, "Cannot inline null node.");
        Node *result = draft_.inlineNode(node, forceSync);
        changed_     = true;
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

    RewriteResult finish() {
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
