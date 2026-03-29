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
 * Updated: Mar. 29, 2026
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
// LayoutResult：computeLayout() 的纯计算输出。
//
// 包含图中所有节点的新 slot 编号映射、静态数据数组、以及各类签名类型，
// 由 applyLayout() 一次性写入 Graph。将 layout 计算与 Graph 变更解耦，
// 使得 sealGraph / exportRoot 等流程可以先计算再搬运。
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
// GraphBuilder：Graph 的唯一底层构造/变换入口。
//
// 职责：
//   1. 创建新图 (createGraph)、克隆图 (cloneGraph)。
//   2. 在 draft（非 finalized）图上执行单图结构编辑：增删节点、端口、闭包、子图、依赖。
//   3. 封印 (sealGraph)：一次性执行布局计算（rearrange + finalized frame layout），然后将所有
//      节点的 draft 邻接 vectors 冻结到 arena 定长数组上。封印后图不再允许编辑。
//
// 设计约束：
//   - GraphBuilder 只负责当前 graph_ 这一张图的底层变换，不负责跨图事务。
//   - 构图期可变状态存放在 GraphBuilderState（定义于 graph.h）中。
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
    /// 封印此图：执行布局计算（rearrange + finalized frame layout），然后将所有节点的
    /// draft 邻接 vectors 搬迁到 arena 上的定长数组。具有 consume 语义：
    /// 调用后 builder 不再可用（graph_ 置 nullptr）。
    void sealGraph();
    /// 递归封印整棵图树（深度优先）。
    static void sealGraphRecursively(const graph_ptr_t &graph);

    /// 纯计算：遍历 graph 的 ports/closure/nodes，生成 slot 编号映射和签名类型，
    /// 不修改 Graph 任何成员。后续由 applyLayout() 一次性写入。
    static LayoutResult computeLayout(const Graph &graph);

    static void validateGraph(const Graph &graph);
    static void validateGraphRecursively(const graph_ptr_t &graph);

  private:
    // 所有写操作先写 GraphBuilderState(staging)，再由 syncStateToGraph 镜像到 Graph 字段。
    // sealed 后 staging 被消费，Graph 回到只读视图。
    /// 内部 seal 状态机：Draft -> Sealing -> Sealed（幂等）。
    /// 顺序固定为：rearrange -> debug promote -> freeze adjacency -> static pack
    /// -> installFinalFrameLayout -> releaseDraftRegion。
    void finalize() const;
    /// 将 computeLayout() 的结果写入 graph：更新每个节点的 dataIndex、
    /// 更新 Graph 的 signature / staticDataArr / finalized frame layout。
    static void applyLayout(Graph &graph, const LayoutResult &layout);
    void rearrange() const;
    void assertBuildable(const char *action) const;
    void markMutated() const;
    GraphBuilderState &state() const;
    static std::shared_ptr<GraphBuilderState> snapshotStateFromGraph(Graph &graph);
    void syncStateToGraph() const;

    Graph *graph_;
};

// GraphDraft 已拆分至 `draft.h`，用于承载跨图 rewrite 会话中的 owned 范围、
// source->draft 规范化与封印导出流程。`builder.h` 仅保留单图底层变换 API。

} // namespace camel::compile::gir
