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
 * Updated: Mar. 15, 2026
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
#include "camel/core/mm.h"
#include "camel/execute/pass/sched.h"

namespace ctx = camel::core::context;

class NodeVMSchedPass : public GraphSchedulePass {
    inline static const size_t maxRecursionDepth_            = 256;
    inline static constexpr std::size_t kTopoNodesExtraIndex = 2;

    size_t currRecursionDepth_ = 0;
    ctx::FramePool framePool_{1 * camel::core::mm::MB};
    // NodeVM 自己维护 topo cache；GIR 本体不保存任何 VM 专属执行缓存。
    std::unordered_set<GIR::Graph *> graphsWithTopoCache_;
    std::unordered_map<GIR::Graph *, std::vector<GIR::Node *>> topoNodesOwned_;

    // 复用 buffer，避免 OPER 分支内每次堆分配（先 norm 后 with，用偏移区分）
    std::vector<GIR::data_idx_t> operIndices_;

    slot_t call(GIR::Graph *graph, ctx::Frame *rootFrame);
    std::span<GIR::Node *> buildTopoNodes(GIR::Graph *graph);

  public:
    NodeVMSchedPass(const ctx::context_ptr_t &ctx) : GraphSchedulePass(ctx) {}
    ~NodeVMSchedPass() override;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;

  private:
    void evalMarkedOperator(const std::string &uri, GIR::Node *node, ctx::Frame &currFrame);

    void evalMarkedOperator_map_arr(GIR::Node *node, ctx::Frame &currFrame);
    void evalMarkedOperator_apply_arr(GIR::Node *node, ctx::Frame &currFrame);
    void evalMarkedOperator_filter_arr(GIR::Node *node, ctx::Frame &currFrame);
    void evalMarkedOperator_reduce_arr(GIR::Node *node, ctx::Frame &currFrame);
    void evalMarkedOperator_foreach_arr(GIR::Node *node, ctx::Frame &currFrame);
};
