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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/execute/pass/sched.h"

#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/taskflow.hpp>
#include <unordered_map>

namespace ctx = camel::core::context;

class TaskflowExecSchedPass : public GraphSchedulePass {
  public:
    TaskflowExecSchedPass(const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks = 32)
        : GraphSchedulePass(ctx), executor_(max_concurrent_tasks) {}
    virtual ~TaskflowExecSchedPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;

    tf::Taskflow mainFlow_; // 主任务流
    ctx::FramePool framePool_{1 * camel::core::mm::MB};

    // 元信息（目前存 BRCH->JOIN 的配对关系）
    struct GraphInfos {
        GIR::Graph *graph{nullptr};
        std::unordered_map<GIR::Node *, GIR::Node *> joinToBrch;
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
    } globalBuildCtx_;

  private:
    tf::Executor executor_;

    // 为一次图实例执行构建并运行任务流，返回 exit 值（slot_t）
    slot_t evalGraphTF(GIR::Graph *graph, ctx::Frame *frame);

    // 递归构建所有图的元信息
    void buildGraphsInfo(GIR::Graph *rootGraph);

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
