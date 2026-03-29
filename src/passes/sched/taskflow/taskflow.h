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
 * Created: Oct. 05, 2025
 * Updated: Mar. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/execute/pass/sched.h"

#include <memory>
#include <mutex>
#include <span>
#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/taskflow.hpp>
#include <unordered_map>
#include <vector>

namespace camel::core::context {

// Taskflow 专用的并发 frame allocator。
// 与通用 FramePool 不同，它允许并发 acquire/release 和乱序回收，
// 以适配 Taskflow worker 的并发执行模型。
class TaskflowFramePool {
  public:
    explicit TaskflowFramePool(
        size_t chunkBytes = 1 * camel::core::mm::MB, size_t minChunkFrames = 8);
    ~TaskflowFramePool();

    TaskflowFramePool(const TaskflowFramePool &)            = delete;
    TaskflowFramePool &operator=(const TaskflowFramePool &) = delete;

    Frame *acquire(GIR::Graph *graph);
    void release(Frame *frame);
    void warmup(GIR::Graph *graph, size_t count);

  private:
    struct GraphArena {
        GIR::Graph *graph{nullptr};
        size_t frameSize{0};
        const camel::core::type::TupleType *runtimeDataType{nullptr};
        ::Tuple *staticArea{nullptr};
        size_t chunkFrames{0};
        std::mutex mutex;
        std::vector<Frame *> freeFrames;
        std::vector<std::byte *> chunks;
    };

    GraphArena &getOrCreateArena(GIR::Graph *graph);
    void allocateChunk(GraphArena &arena, size_t minFrameCount);

    size_t chunkBytes_;
    size_t minChunkFrames_;
    std::mutex arenasMutex_;
    std::unordered_map<GIR::Graph *, std::unique_ptr<GraphArena>> arenas_;
};

} // namespace camel::core::context

namespace ctx = camel::core::context;

class TaskflowExecSchedPass : public GraphSchedulePass {
  public:
    TaskflowExecSchedPass(const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks = 32)
        : GraphSchedulePass(ctx), executor_(max_concurrent_tasks) {}
    virtual ~TaskflowExecSchedPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;

    /// 仅构建任务流并 dump 为 DOT 到 os，不执行。供 TfDumpPass 等使用。
    void buildAndDump(GIR::Graph *graph, std::ostream &os);

    tf::Taskflow mainFlow_; // 主任务流
    ctx::TaskflowFramePool framePool_{1 * camel::core::mm::MB};

    // 预编译元信息：BRCH/JOIN 关系、输入索引映射、端口与闭包布局等
    struct GraphInfos {
        struct NodeExecMeta {
            std::vector<GIR::data_idx_t> normIndices;
            std::vector<GIR::data_idx_t> withIndices;
        };

        GIR::Graph *graph{nullptr};
        std::unordered_map<GIR::Node *, GIR::Node *> joinToBrch;
        std::unordered_map<GIR::Node *, NodeExecMeta> nodeExecMeta;
        std::vector<GIR::data_idx_t> normPortIndices;
        std::vector<GIR::data_idx_t> withPortIndices;
        std::vector<GIR::data_idx_t> closureIndices;

        NodeExecMeta &getOrCreateNodeExecMeta(GIR::Node *node) { return nodeExecMeta[node]; }

        const NodeExecMeta &getNodeExecMeta(GIR::Node *node) const {
            auto it = nodeExecMeta.find(node);
            ASSERT(it != nodeExecMeta.end(), "Node exec meta not found.");
            return it->second;
        }
    };

    struct GlobalBuildCtx {
        std::unordered_map<GIR::Graph *, std::unique_ptr<GraphInfos>> graphInfoMap;
        std::unordered_set<GIR::Node *> skipNodes;

        GraphInfos &getOrCreateGraphInfos(GIR::Graph *graph) {
            auto it = graphInfoMap.find(graph);
            if (it == graphInfoMap.end()) {
                auto tasks          = std::make_unique<GraphInfos>();
                tasks->graph        = graph;
                auto &ref           = *tasks;
                graphInfoMap[graph] = std::move(tasks);
                return ref;
            }
            return *it->second;
        }

        GraphInfos &getGraphInfos(GIR::Graph *graph) {
            auto it = graphInfoMap.find(graph);
            ASSERT(it != graphInfoMap.end(), "Graph tasks not found.");
            return *it->second;
        }

        const GraphInfos &getGraphInfos(GIR::Graph *graph) const {
            auto it = graphInfoMap.find(graph);
            ASSERT(it != graphInfoMap.end(), "Graph tasks not found.");
            return *it->second;
        }
    } globalBuildCtx_;

  private:
    tf::Executor executor_;

    // 为一次图实例执行构建并运行任务流，返回 exit 值（slot_t）
    slot_t evalGraphTF(GIR::Graph *graph, ctx::Frame *frame);

    // 递归构建所有图的元信息
    void buildGraphsInfo(GIR::Graph *rootGraph);

    // 统一子图执行入口：接管已准备好的 frame 生命周期
    slot_t runPreparedSubgraph(tf::Subflow &sf, GIR::Graph *graph, ctx::Frame *frame);

    // 预编译的调用布局填参
    ctx::Frame *acquirePreparedNodeCallFrame(
        GIR::Graph *targetGraph, GIR::Node *callNode, ctx::Frame *sourceFrame);
    ctx::Frame *acquirePreparedClosureCallFrame(
        GIR::Graph *targetGraph, ::Tuple *closure, std::span<const slot_t> args);

    // 复用预编译后的参数索引，减少运行时 vector 构造
    slot_t executePreparedOperator(GIR::Node *n, ctx::Frame *frame);

    // 通用：在任意 flowLike(可为 Taskflow/Subflow) 中展开一次图实例
    template <typename FlowT>
    void instantiate_graph_instance_generic(FlowT &flowLike, GIR::Graph *graph, ctx::Frame *frame);

    // 分离的节点任务构建（每种类型一个函数）
    template <typename FlowT>
    tf::Task buildExitTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildDataTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildPortTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCopyTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCastTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildFillTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildAccsTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildFuncTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCallTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildOperTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    // BRCH-JOIN 区域处理（创建 selector/candidate/join 任务）
    template <typename FlowT>
    void buildBranchJoinRegion(
        FlowT &flowLike, GIR::Graph *graph, ctx::Frame *frame,
        std::unordered_map<GIR::Node *, tf::Task> &taskMap, GIR::Node *brch);

    // 构建非 BRCH-JOIN 的普通节点任务（含 ports）
    template <typename FlowT>
    void buildNormalNodeTasks(
        FlowT &flowLike, GIR::Graph *graph, ctx::Frame *frame,
        std::unordered_map<GIR::Node *, tf::Task> &taskMap);

    // 连接依赖边
    template <typename FlowT>
    void connectDependencies(
        FlowT &flowLike, GIR::Graph *graph, std::unordered_map<GIR::Node *, tf::Task> &taskMap);

    // 标记算子（使用 Subflow 并行元素任务）
    void mark_map_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_apply_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_filter_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_reduce_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_foreach_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_unordered_foreach_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_unordered_reduce_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
};

/// 将 Taskflow 执行图 dump 为 GraphViz DOT 格式，便于用 dot 等工具可视化。不执行图。
class TfDumpPass : public GraphIRPass {
  public:
    TfDumpPass(const ctx::context_ptr_t &ctx) : GraphIRPass(ctx) {}
    virtual ~TfDumpPass() = default;
    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;
};
