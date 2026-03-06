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
 * Updated: Mar. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/execute/pass/sched.h"

#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/taskflow.hpp>
#include <unordered_map>

class TaskflowExecSchedPass : public GraphSchedulePass {
  public:
    TaskflowExecSchedPass(const context_ptr_t &ctx, size_t max_concurrent_tasks = 32)
        : GraphSchedulePass(ctx), executor_(max_concurrent_tasks) {}
    virtual ~TaskflowExecSchedPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;

    tf::Taskflow mainFlow_; // 主任务流
    FramePool framePool_{1 * MB};

    // 元信息（目前存 BRCH->JOIN 的配对关系）
    struct GraphInfos {
        GraphIR::Graph *graph{nullptr};
        std::unordered_map<GraphIR::Node *, GraphIR::Node *> joinToBrch;
    };

    struct GlobalBuildCtx {
        std::unordered_map<GraphIR::Graph *, std::unique_ptr<GraphInfos>> graphInfoMap;
        std::unordered_set<GraphIR::Node *> skipNodes;

        GraphInfos &getOrCreateGraphInfos(GraphIR::Graph *graph) {
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

        GraphInfos &getGraphInfos(GraphIR::Graph *graph) {
            auto it = graphInfoMap.find(graph);
            ASSERT(it != graphInfoMap.end(), "Graph tasks not found.");
            return *it->second;
        }
    } globalBuildCtx_;

  private:
    tf::Executor executor_;

    // 为一次图实例执行构建并运行任务流，返回 exit 值（slot_t）
    slot_t evalGraphTF(GraphIR::Graph *graph, Frame *frame);

    // 递归构建所有图的元信息
    void buildGraphsInfo(GraphIR::Graph *rootGraph);

    // 通用：在任意 flowLike(可为 Taskflow/Subflow) 中展开一次图实例
    template <typename FlowT>
    void instantiate_graph_instance_generic(FlowT &flowLike, GraphIR::Graph *graph, Frame *frame);

    // 分离的节点任务构建（每种类型一个函数）
    template <typename FlowT>
    tf::Task buildExitTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildDataTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildPortTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildCopyTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildCastTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildFillTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildAccsTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildFuncTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildCallTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    template <typename FlowT>
    tf::Task buildOperTask(FlowT &flowLike, GraphIR::Node *n, Frame *frame);

    // BRCH-JOIN 区域处理（创建 selector/candidate/join 任务）
    template <typename FlowT>
    void buildBranchJoinRegion(
        FlowT &flowLike, GraphIR::Graph *graph, Frame *frame,
        std::unordered_map<GraphIR::Node *, tf::Task> &taskMap, GraphIR::Node *brch);

    // 构建非 BRCH-JOIN 的普通节点任务（含 ports）
    template <typename FlowT>
    void buildNormalNodeTasks(
        FlowT &flowLike, GraphIR::Graph *graph, Frame *frame,
        std::unordered_map<GraphIR::Node *, tf::Task> &taskMap);

    // 连接依赖边
    template <typename FlowT>
    void connectDependencies(
        FlowT &flowLike, GraphIR::Graph *graph,
        std::unordered_map<GraphIR::Node *, tf::Task> &taskMap);

    // 标记算子（使用 Subflow 并行元素任务）
    void mark_map_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
    void mark_apply_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
    void mark_filter_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
    void mark_reduce_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
    void mark_foreach_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
    void mark_unordered_foreach_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
    void mark_unordered_reduce_arr(GraphIR::Node *node, Frame *frame, tf::Subflow &sf);
};
