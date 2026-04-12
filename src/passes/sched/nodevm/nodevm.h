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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/execute/pass/runtime_sched.h"
#include "camel/runtime/graph.h"

namespace ctx            = camel::core::context;
using runtime_data_idx_t = camel::runtime::gc_data_idx_t;

struct NodeVMGraphCache {
    std::vector<camel::runtime::gc_node_ref_t> topoNodeRefs;
    std::vector<camel::runtime::GCGraph *> directCallTargets;
    std::vector<uint8_t> directCallTailEligible;
    std::vector<uint8_t> directCallFeedsTailJoin;
    std::vector<uint32_t> directCallArgOffsets;
    std::vector<runtime_data_idx_t> directCallArgSlots;
    std::vector<runtime_data_idx_t> dataIndexByRef;
    camel::runtime::gc_node_ref_t tailValueRef = camel::runtime::kInvalidNodeRef;
    size_t tailValueTopoIndex                  = 0;
    bool tailValueIsJoin                       = false;
};

struct NodeVMCallLayoutCache {
    std::vector<runtime_data_idx_t> calleePortSlots;
};

class NodeVMSchedPass : public RuntimeGraphSchedulePass {
    inline static const size_t maxRecursionDepth_ = 256;

    size_t currRecursionDepth_ = 0;
    ctx::FramePool framePool_{1 * camel::core::mm::MB};
    std::vector<std::unique_ptr<NodeVMGraphCache>> graphCaches_;
    std::vector<std::unique_ptr<NodeVMCallLayoutCache>> callLayoutCaches_;
    std::vector<runtime_data_idx_t> operIndices_;

    slot_t call(camel::runtime::GCGraph *runtimeGraph, ctx::Frame *rootFrame);
    std::span<const camel::runtime::gc_node_ref_t> buildTopoNodes(camel::runtime::GCGraph *graph);
    std::span<const camel::runtime::gc_node_ref_t> topoNodesFor(camel::runtime::GCGraph *graph);

  public:
    NodeVMSchedPass(const ctx::context_ptr_t &ctx) : RuntimeGraphSchedulePass(ctx) {}
    ~NodeVMSchedPass() override;

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;

  private:
    void evalMarkedOperator(
        const std::string &uri, camel::runtime::GCGraph *graph,
        camel::runtime::gc_node_ref_t nodeRef, ctx::Frame &currFrame);
    void evalMarkedOperator_map_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    void evalMarkedOperator_apply_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    void evalMarkedOperator_filter_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    void evalMarkedOperator_reduce_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    void evalMarkedOperator_foreach_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
};
