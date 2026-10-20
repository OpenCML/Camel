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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/limits.h"
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
    // For the BRCH at topo index i, branchArmEntries[branchArmEntryOffsets[i] + arm] is the node
    // where that arm starts executing: its first executable node in topoNodeRefs, or the JOIN
    // when the arm has nothing to execute (resolveRuntimeBranchArmEntry, shared with FastVM).
    std::vector<uint32_t> branchArmEntryOffsets;
    std::vector<camel::runtime::gc_node_ref_t> branchArmEntries;
    camel::runtime::gc_node_ref_t tailValueRef = camel::runtime::kInvalidNodeRef;
    size_t tailValueTopoIndex                  = 0;
    bool tailValueIsJoin                       = false;
};

struct NodeVMCallLayoutCache {
    std::vector<runtime_data_idx_t> calleePortSlots;
};

class NodeVMSchedPass : public RuntimeGraphSchedulePass {
    // Calls that may be open at once without being tail calls (camel/execute/limits.h).
    size_t maxRecursionDepth_;

    // Activations of call() on the native stack, the entry graph's included.
    size_t currRecursionDepth_ = 0;
    ctx::FramePool framePool_;
    // The programs whose graphs this run executes (a frozen graph names its callees by id).
    camel::execute::RunPrograms programs_;
    std::vector<std::unique_ptr<NodeVMGraphCache>> graphCaches_;
    std::vector<std::unique_ptr<NodeVMCallLayoutCache>> callLayoutCaches_;
    std::vector<runtime_data_idx_t> operIndices_;
    std::vector<slot_t> callArgScratch_;

    slot_t call(camel::runtime::GCGraph *runtimeGraph, ctx::Frame *rootFrame);
    // Raises MaxRecursionDepthExceeded when one more non-tail call would exceed the limit, or
    // would not fit the native stack (every call() nests on it). It is checked in the caller,
    // before the call, so the diagnostic points at the call expression (as FastVM's does) and
    // not at the callee's definition. The activation of the entry graph is not a call of the
    // program and is not counted.
    void requireRecursionRoom(const camel::runtime::GCGraph *caller) const;
    // Tail-call frame management (see the comment above call() in nodevm.cpp).
    ctx::Frame *tailCallFrame(
        camel::runtime::GCGraph *target, ctx::Frame *rootFrame, ctx::Frame *source,
        ctx::Frame *&tailLow, ctx::Frame *&tailHigh, const NodeVMCallLayoutCache *layout,
        std::span<const runtime_data_idx_t> argSlots);
    void releaseTailFrames(ctx::Frame *&tailLow, ctx::Frame *&tailHigh);
    std::span<const camel::runtime::gc_node_ref_t> buildTopoNodes(camel::runtime::GCGraph *graph);
    std::span<const camel::runtime::gc_node_ref_t> topoNodesFor(camel::runtime::GCGraph *graph);

  public:
    NodeVMSchedPass(const ctx::context_ptr_t &ctx);
    ~NodeVMSchedPass() override;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    __attribute__((noinline)) slot_t evalNativeOperator(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame &frame,
        operator_t op);
    /// Runs a marked operator (camel/core/operator_table.h), which calls the function values it
    /// receives. Kept out of line on purpose: call() recurses on the native stack once per Camel
    /// call, and these helpers run once per higher-order operator, so inlining them (with their
    /// locals) into call() would only enlarge every recursion level.
    __attribute__((noinline)) void evalMarkedOperator(
        camel::core::OperId oper, camel::runtime::GCGraph *graph,
        camel::runtime::gc_node_ref_t nodeRef, ctx::Frame &currFrame);
    __attribute__((noinline)) void evalMarkedOperator_map_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    __attribute__((noinline)) void evalMarkedOperator_filter_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    __attribute__((noinline)) void evalMarkedOperator_reduce_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    __attribute__((noinline)) void evalMarkedOperator_foreach_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    __attribute__((noinline)) void evalMarkedOperator_isolate(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
    /// Runs the LOOP node `nodeRef` (camel/runtime/loop.h) natively: one call of the condition
    /// graph and one of the body graph per round, the state kept in the node's own slot of
    /// `currFrame` (so the collector traces it). A round is not a recursion: each call is one
    /// level deep and returns before the next, so no number of rounds reaches the limit.
    __attribute__((noinline)) void evalLoop(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame &currFrame);
};
