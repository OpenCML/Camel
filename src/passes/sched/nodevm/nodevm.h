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
 * Updated: Mar. 06, 2026
 * Supported by: National Key Research and Development Program of China
 *
 * NodeVM：在图节点上直接执行，无字节码编译。用于展示在 GIR 上直接执行的流程。
 */

#pragma once

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "camel/compile/gir/types.h"
#include "camel/core/context/frame.h"
#include "camel/execute/pass/sched.h"

class NodeVMSchedPass : public GraphSchedulePass {
    inline static const size_t maxRecursionDepth_ = 256;

    size_t currRecursionDepth_ = 0;
    FramePool framePool_{1 * MB};
    // 热路径走 Graph extra 的 O(1) 缓存；冷路径写入 topoNodesOwned_ 并挂到 extra 上
    std::unordered_set<GraphIR::Graph *> graphsWithTopoCache_;
    std::unordered_map<GraphIR::Graph *, std::vector<GraphIR::Node *>> topoNodesOwned_;

    // 复用 buffer，避免 OPER 分支内每次堆分配（先 norm 后 with，用偏移区分）
    std::vector<GraphIR::data_idx_t> operIndices_;

    static constexpr size_t kTopoNodesExtraIndex = 1;

    slot_t call(GraphIR::Graph *graph, Frame *rootFrame);
    std::span<GraphIR::Node *> buildTopoNodes(GraphIR::Graph *graph);

  public:
    NodeVMSchedPass(const context_ptr_t &ctx) : GraphSchedulePass(ctx) {}
    ~NodeVMSchedPass() override;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;

  private:
    void evalMarkedOperator(const std::string &uri, GraphIR::Node *node, Frame &currFrame);

    void evalMarkedOperator_map_arr(GraphIR::Node *node, Frame &currFrame);
    void evalMarkedOperator_apply_arr(GraphIR::Node *node, Frame &currFrame);
    void evalMarkedOperator_filter_arr(GraphIR::Node *node, Frame &currFrame);
    void evalMarkedOperator_reduce_arr(GraphIR::Node *node, Frame &currFrame);
    void evalMarkedOperator_foreach_arr(GraphIR::Node *node, Frame &currFrame);
};
