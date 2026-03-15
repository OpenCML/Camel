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

using type_vec_t = camel::core::type::type_vec_t;

// =============================================================================
// LayoutResult：computeLayout() 的纯计算输出。
//
// 包含图中所有节点的新 slot 编号映射、静态数据数组、以及各类签名类型，
// 由 applyLayout() 一次性写入 Graph。将 layout 计算与 Graph 变更解耦，
// 使得 sealGraph / exportRoot 等流程可以先计算再搬运。
// =============================================================================
struct LayoutResult {
    std::vector<std::pair<Node *, data_idx_t>> nodeIndices;
    data_vec_t staticDataArr;
    TupleType *staticDataType  = nullptr;
    TupleType *runtimeDataType = nullptr;
    TupleType *closureType     = nullptr;
    size_t runtimeDataSize     = 1;
};

// =============================================================================
// GraphBuilder：Graph 的唯一底层构造/变换入口。
//
// 职责：
//   1. 创建新图 (createGraph)、克隆图 (cloneGraph)。
//   2. 在 draft（非 finalized）图上执行结构编辑：增删节点、端口、闭包、子图、依赖。
//   3. 封印 (sealGraph)：一次性执行布局计算（rearrange + FrameMeta），然后将所有
//      节点的 draft 邻接 vectors 冻结到 arena 定长数组上。封印后图不再允许编辑。
//
// 设计约束：
//   - Graph 自身不暴露任何 mutable 接口，所有写操作均通过 GraphBuilder (friend)。
//   - sealGraph 是单向终结。要修改已封印的图必须先 cloneGraph 出 draft 副本。
//   - computeLayout() / applyLayout() 将布局计算与变更解耦。
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
    /// 封印此图：执行布局计算（rearrange + FrameMeta），然后将所有节点的
    /// draft 邻接 vectors 搬迁到 arena 上的定长数组。具有 consume 语义：
    /// 调用后 builder 不再可用（graph_ 置 nullptr）。
    void sealGraph();
    /// 递归封印整棵图树（深度优先）。
    static void sealGraphRecursively(const graph_ptr_t &graph);

    /// 纯计算：遍历 graph 的 ports/closure/nodes，生成 slot 编号映射和签名类型，
    /// 不修改 Graph 任何成员。后续由 applyLayout() 一次性写入。
    static LayoutResult computeLayout(const Graph &graph);

    /// 将 computeLayout() 的结果写入 graph：更新每个节点的 dataIndex、
    /// 更新 Graph 的 signature / staticDataArr / FrameMeta。
    static void applyLayout(Graph &graph, const LayoutResult &layout);

  private:
    /// 内部：执行 rearrange + installFrameMeta 并标记 finalized。幂等。
    void finalize() const;
    void rearrange() const;
    void assertBuildable(const char *action) const;
    void markMutated() const;

    Graph *graph_;
};

// =============================================================================
// GraphDraft：一次 rewrite 的工作态。
//
// GraphDraft 持有 source graph 的 cloneGraph 工作副本（draft-owned），并负责：
//   1. 约束所有编辑只能发生在 draft-owned graph 上（assertDraftOwned）。
//   2. 编辑结束后由 seal() 一次性封印整棵子图树。
//   3. 为 GraphRewriteSession 提供 graph-in / graph-out 的内部编辑原语。
//
// 使用流程：
//   GraphDraft draft(frozenGraph);        // 1. 从 frozen 源图克隆出 draft
//   draft.eraseNode(n);                   // 2. 在 draft 上执行编辑
//   draft.replaceNode(old, new);
//   draft.seal();                         // 3. 封印：rearrange + FrameMeta + freeze
//   return draft.root();                  // 4. 返回新的 sealed graph
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
    /// 统一封印导出：递归对整棵子图树执行 sealGraph（含 rearrange + FrameMeta + freeze）。
    /// sealGraph 内部调用 finalize（幂等），对已 finalized 的图自动跳过布局阶段，
    /// 但仍执行 freeze 邻接表搬迁。
    void seal() {
        dirtyGraphs_.clear();
        GraphBuilder::sealGraphRecursively(root_);
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
