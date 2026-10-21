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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Computation of the execution plan of a graph version (camel/runtime/exec_plan.h).
 */

#include "camel/runtime/exec_plan.h"

#include "camel/execute/graph_runtime_support.h"

#include <algorithm>

namespace camel::runtime {

namespace {

gc_data_idx_t dataIndexOf(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    ASSERT(node != nullptr, "Execution plan node lookup resolved to null.");
    return node->dataIndex;
}

} // namespace

const std::vector<ExecNode> &ExecPlan::buildNodes() const {
    std::lock_guard lock(nodesMutex_);
    if (!nodesReady_.load(std::memory_order_relaxed)) {
        const size_t count = topo.size();
        nodes_.resize(count);
        for (size_t idx = 0; idx < count; ++idx) {
            const gc_node_ref_t ref = topo[idx];
            ExecNode &en            = nodes_[idx];
            en.ref                  = ref;
            en.node                 = graph->node(ref);
            ASSERT(en.node != nullptr, "Execution plan node lookup resolved to null.");
            const auto norm = graph->normInputsOf(ref);
            const auto with = graph->withInputsOf(ref);
            en.norm         = norm.data();
            en.normCount    = static_cast<uint32_t>(norm.size());
            en.with         = with.data();
            en.withCount    = static_cast<uint32_t>(with.size());
            if (en.node->hasPayload()) {
                en.body = graph->nodePayloadBytes(ref).data();
            }
            if (en.node->kind == GCNodeKind::Brch) {
                const auto arms = graph->branchArmsOf(ref);
                en.arms         = arms.data();
                en.armCount     = static_cast<uint32_t>(arms.size());
            }
        }
        nodesReady_.store(true, std::memory_order_release);
    }
    return nodes_;
}

std::unique_ptr<ExecPlan> buildExecPlan(const GCGraph &graph) {
    // The helpers of graph_runtime_support read the graph through its const interface only.
    auto *mutableGraph = const_cast<GCGraph *>(&graph);
    auto plan          = std::make_unique<ExecPlan>();
    plan->graph        = &graph;
    plan->topo         = camel::execute::computeExecutionOrder(&graph);

    const size_t count        = plan->topo.size();
    const bool hasValueReturn = graph.funcType() != nullptr && graph.funcType()->hasExitType() &&
                                graph.funcType()->exitType() != camel::core::type::Type::Void();

    plan->directCallTargetIds.resize(count, kInvalidGraphId);
    plan->directCallTailEligible.resize(count, 0);
    plan->directCallFeedsTailJoin.resize(count, 0);
    plan->directCallArgOffsets.resize(count + 1, 0);
    plan->branchArmEntryOffsets.resize(count + 1, 0);
    plan->dataIndexByRef.resize(graph.nodeCount(), 0);
    plan->resolvedOpers = std::make_unique<std::atomic<operator_t>[]>(graph.nodeCount());
    plan->topoIndexByRef.assign(graph.nodeCount(), ExecPlan::kNotInPlan);
    for (size_t idx = 0; idx < count; ++idx) {
        plan->topoIndexByRef[plan->topo[idx]] = static_cast<uint32_t>(idx);
    }
    plan->tailValueRef = hasValueReturn ? camel::execute::resolveRuntimeTailValueRef(mutableGraph)
                                        : kInvalidNodeRef;
    const GCNode *tailValueNode =
        plan->tailValueRef != kInvalidNodeRef ? graph.node(plan->tailValueRef) : nullptr;
    if (!tailValueNode || tailValueNode->dataIndex == 0) {
        plan->tailValueRef = kInvalidNodeRef;
        tailValueNode      = nullptr;
    }
    plan->tailValueIsJoin    = tailValueNode != nullptr && tailValueNode->kind == GCNodeKind::Join;
    plan->tailValueTopoIndex = count;
    for (size_t idx = 0; idx < count; ++idx) {
        if (plan->topo[idx] == plan->tailValueRef) {
            plan->tailValueTopoIndex = idx;
            break;
        }
    }
    const bool anchorOk = plan->tailValueTopoIndex < count &&
                          camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
                              &graph,
                              plan->topo,
                              plan->tailValueTopoIndex);

    // A call whose result reaches the tail JOIN only through forwarding GATEs and nested JOINs
    // is still in tail position.
    const std::vector<gc_node_ref_t> tailJoinArmValues =
        anchorOk && plan->tailValueIsJoin
            ? camel::execute::collectRuntimeTailJoinArmValues(mutableGraph, plan->tailValueRef)
            : std::vector<gc_node_ref_t>{};

    for (size_t idx = 0; idx < count; ++idx) {
        const gc_node_ref_t ref = plan->topo[idx];
        const GCNode *node      = graph.node(ref);
        if (node) {
            plan->dataIndexByRef[ref] = node->dataIndex;
        }
        plan->directCallArgOffsets[idx]    = static_cast<uint32_t>(plan->directCallArgSlots.size());
        plan->branchArmEntryOffsets[idx]   = static_cast<uint32_t>(plan->branchArmEntries.size());
        if (node && node->kind == GCNodeKind::Brch) {
            const size_t armCount = graph.branchArmsOf(ref).size();
            for (size_t arm = 0; arm < armCount; ++arm) {
                plan->branchArmEntries.push_back(
                    camel::execute::resolveRuntimeBranchArmEntry(
                        mutableGraph,
                        ref,
                        arm,
                        plan->topo));
            }
        }
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        plan->directCallTargetIds[idx] = graph.calleeIdOf(ref);
        ASSERT(
            plan->directCallTargetIds[idx] != kInvalidGraphId,
            "A direct FUNC target must be materialized into runtime metadata.");
        for (gc_node_ref_t argRef : graph.normInputsOf(ref)) {
            plan->directCallArgSlots.push_back(dataIndexOf(graph, argRef));
        }
        for (gc_node_ref_t argRef : graph.withInputsOf(ref)) {
            plan->directCallArgSlots.push_back(dataIndexOf(graph, argRef));
        }
        plan->directCallTailEligible[idx]  = anchorOk && (ref == plan->tailValueRef);
        plan->directCallFeedsTailJoin[idx] =
            std::find(tailJoinArmValues.begin(), tailJoinArmValues.end(), ref) !=
            tailJoinArmValues.end();
    }
    plan->tailAnchorOk = anchorOk;
    if (anchorOk) {
        if (plan->tailValueIsJoin) {
            plan->tailJoins.push_back(plan->tailValueRef);
        }
        for (const gc_node_ref_t value : tailJoinArmValues) {
            if (graph.node(value)->kind == GCNodeKind::Join) {
                plan->tailJoins.push_back(value);
            }
        }
        for (size_t idx = 0; idx < count; ++idx) {
            if (plan->directCallTailEligible[idx] || plan->directCallFeedsTailJoin[idx]) {
                plan->tailCalls.push_back(plan->topo[idx]);
            }
        }
        std::ranges::sort(plan->tailCalls);
    }
    plan->directCallArgOffsets[count]  = static_cast<uint32_t>(plan->directCallArgSlots.size());
    plan->branchArmEntryOffsets[count] = static_cast<uint32_t>(plan->branchArmEntries.size());

    for (gc_node_ref_t portRef : graph.normPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    for (gc_node_ref_t portRef : graph.withPorts()) {
        plan->calleePortSlots.push_back(dataIndexOf(graph, portRef));
    }
    return plan;
}

} // namespace camel::runtime
