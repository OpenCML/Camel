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
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "nodes.h"

#include <unordered_set>

namespace camel::compile::gir {

// =============================================================================
// GraphBuilder：Graph 的唯一底层构造/变换入口。
//
// 职责：
//   1. 创建新图 (createGraph)、克隆图 (cloneGraph)。
//   2. 在非 finalized 图上执行结构编辑：增删节点、端口、闭包、子图、依赖。
//   3. 一次性导出终态布局 (finalize)：重排 slot 编号、生成 signature/layout、
//      安装 FrameMeta。finalize 后图标记为 finalized，不再允许后续编辑。
//
// 设计约束：
//   - Graph 自身不暴露任何 mutable 接口，所有写操作均通过 GraphBuilder (friend)。
//   - finalize 是单向终结，不可 "un-finalize"。要修改已终结图必须先 cloneGraph。
//   - rearrange() 仅在 finalize 内部调用，不对外暴露。
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
    static graph_ptr_t cloneGraph(const graph_ptr_t &graph);
    static void finalizeGraph(const graph_ptr_t &graph);
    static void finalizeGraphRecursively(const graph_ptr_t &graph);

    Graph &graph() const { return *graph_; }
    graph_ptr_t graphPtr() const { return graph().shared_from_this(); }

    camel::core::context::FrameMeta *ensureFrameMeta() const;

    data_idx_t addStaticData(const data_ptr_t &data) const;
    data_idx_t addRuntimeData() const;
    void setStaticData(data_idx_t index, const data_ptr_t &data) const;

    Node *ownNode(node_uptr_t node) const;
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

    void touch() const { markMutated(); }
    Node *inlineNode(Node *node, bool forceSync = false) const;
    void finalize() const;
    void finalizeRecursively() const;

  private:
    void rearrange() const;
    void assertBuildable(const char *action) const;
    void markMutated() const;

    Graph *graph_;
};

// =============================================================================
// GraphDraft：一次 rewrite / build 的工作态。
//
// GraphDraft 持有 source graph 的 cloneGraph 工作副本（draft-owned），并负责：
//   1. 约束所有编辑只能发生在 draft-owned graph 上（assertDraftOwned）。
//   2. 记录哪些 graph 被改动（dirtyGraphs_），延迟到 finalize 时统一导出。
//   3. 为 GraphRewriteSession 提供 graph-in / graph-out 的内部编辑原语。
//
// 使用流程：
//   GraphDraft draft(frozenGraph);        // 1. 从 frozen 源图创建 draft
//   draft.eraseNode(n);                   // 2. 在 draft 上执行编辑
//   draft.replaceNode(old, new);
//   draft.finalize();                     // 3. 一次性导出：rearrange + FrameMeta
//   return draft.root();                  // 4. 返回新的 finalized graph
//
// draft 内的编辑通过 GraphBuilder 的底层 API 完成，但 draft 保证只操作克隆副本，
// 绝不触碰原始 source graph。
// =============================================================================
class GraphDraft {
  public:
    explicit GraphDraft(const graph_ptr_t &source)
        : sourceRoot_(source), root_(GraphBuilder::cloneGraph(source)) {
        registerOwnedGraphs(root_);
    }

    graph_ptr_t sourceRoot() const { return sourceRoot_; }
    graph_ptr_t root() const { return root_; }

    bool owns(const Graph *graph) const {
        return graph != nullptr && ownedGraphs_.contains(const_cast<Graph *>(graph));
    }

    void eraseNode(Node *node) {
        assertDraftOwned(&node->graph(), "erase node from");
        GraphBuilder(&node->graph()).eraseNode(node);
        markDirty(&node->graph());
    }
    bool replaceNode(Node *oldNode, Node *newNode) {
        assertDraftOwned(&oldNode->graph(), "replace node on");
        assertDraftOwned(&newNode->graph(), "replace node on");
        bool changed = NodeMutation::replace(oldNode, newNode);
        GraphBuilder(&oldNode->graph()).touch();
        markDirty(&oldNode->graph());
        eraseNode(oldNode);
        return changed;
    }
    void addDependency(const graph_ptr_t &owner, const graph_ptr_t &dep) {
        assertDraftOwned(owner.get(), "add dependency to");
        assertDraftOwned(dep.get(), "add dependency from");
        GraphBuilder(owner).addDependency(dep);
        markDirty(owner.get());
    }
    void eraseDependency(const graph_ptr_t &owner, const graph_ptr_t &dep) {
        assertDraftOwned(owner.get(), "erase dependency from");
        assertDraftOwned(dep.get(), "erase dependency from");
        GraphBuilder(owner).eraseDependency(dep);
        markDirty(owner.get());
    }
    void eraseSubGraph(const graph_ptr_t &owner, const graph_ptr_t &subGraph) {
        assertDraftOwned(owner.get(), "erase subgraph from");
        assertDraftOwned(subGraph.get(), "erase subgraph from");
        GraphBuilder(owner).eraseSubGraph(subGraph);
        markDirty(owner.get());
    }
    void link(LinkType type, Node *from, Node *to) {
        assertDraftOwned(&from->graph(), "link from");
        assertDraftOwned(&to->graph(), "link to");
        NodeMutation::link(type, from, to);
        GraphBuilder(&from->graph()).touch();
        markDirty(&from->graph());
    }
    void replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
        assertDraftOwned(&owner->graph(), "replace input on");
        assertDraftOwned(&oldInput->graph(), "replace input on");
        assertDraftOwned(&newInput->graph(), "replace input on");
        NodeMutation::replaceInput(type, owner, oldInput, newInput);
        GraphBuilder(&owner->graph()).touch();
        markDirty(&owner->graph());
    }
    void replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
        assertDraftOwned(&owner->graph(), "replace output on");
        assertDraftOwned(&oldOutput->graph(), "replace output on");
        assertDraftOwned(&newOutput->graph(), "replace output on");
        NodeMutation::replaceOutput(type, owner, oldOutput, newOutput);
        GraphBuilder(&owner->graph()).touch();
        markDirty(&owner->graph());
    }
    Node *inlineNode(Node *node, bool forceSync = false) {
        assertDraftOwned(&node->graph(), "inline into");
        Node *result = GraphBuilder(&node->graph()).inlineNode(node, forceSync);
        markDirty(&node->graph());
        return result;
    }
    /// 统一导出：对所有 dirty graph 执行 finalize，然后递归 finalize 整棵子图树。
    void finalize() {
        for (Graph *graph : dirtyGraphs_) {
            GraphBuilder(graph).finalize();
        }
        GraphBuilder::finalizeGraphRecursively(root_);
        dirtyGraphs_.clear();
    }

  private:
    void registerOwnedGraphs(const graph_ptr_t &graph) {
        if (!graph || !ownedGraphs_.insert(graph.get()).second) {
            return;
        }
        for (const auto &[_, subGraphs] : graph->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                registerOwnedGraphs(subGraph);
            }
        }
        for (const auto &dep : graph->dependencies()) {
            registerOwnedGraphs(dep);
        }
    }
    void assertDraftOwned(const Graph *graph, const char *action) const {
        ASSERT(
            owns(graph),
            std::format(
                "GraphDraft cannot {} non-draft graph '{}'.",
                action,
                graph ? graph->name() : "<null>"));
    }
    void markDirty(Graph *graph) { dirtyGraphs_.insert(graph); }

    graph_ptr_t sourceRoot_;
    graph_ptr_t root_;
    std::unordered_set<Graph *> ownedGraphs_;
    std::unordered_set<Graph *> dirtyGraphs_;
};

} // namespace camel::compile::gir
