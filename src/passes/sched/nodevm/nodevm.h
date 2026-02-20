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
 * Created: Sep. 08, 2025
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 *
 * NodeVM：在图节点上直接执行，无字节码编译。用于展示在 GIR 上直接执行的流程。
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/execute/pass/sched.h"

class NodeVMSchedPass : public GraphSchedulePass {
    inline static const size_t maxRecursionDepth_ = 256;

    size_t currRecursionDepth_ = 0;
    FramePool framePool_{1 * MB};
    std::unordered_map<GraphIR::Graph *, std::shared_ptr<GraphIR::node_vec_t>> graphTopoNodesCache_;

    slot_t call(GraphIR::Graph *graph, Frame *rootFrame);
    std::shared_ptr<GraphIR::node_vec_t> getTopoNodes(GraphIR::Graph *graph);

    /// 执行一次 CALL 或 FUNC 节点，返回调用结果（供 JOIN 与 CALL/FUNC 使用）
    slot_t doCall(const GraphIR::node_ptr_t &n, Frame *currFrame);

    void
    evalMarkedOperator(const std::string &uri, const GraphIR::node_ptr_t &node, Frame &currFrame);

    void evalMarkedOperator_map_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_apply_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_filter_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_reduce_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_foreach_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);

  public:
    NodeVMSchedPass(const context_ptr_t &ctx) : GraphSchedulePass(ctx) {}
    virtual ~NodeVMSchedPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
