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
 * The execution plan of a graph version (docs/design/graph-store 4.6).
 *
 * What every back end needs to run a graph and used to compute for itself: the order its live
 * nodes run in, where each arm of a branch starts, the callee and the argument slots of each
 * direct call, which calls are in tail position. It is computed once, the first time a back end
 * asks (GCGraph::execPlan), from the frozen graph alone, and kept with the graph version: a
 * rewrite makes a new version, which gets a new plan. Only the nodes the exit depends on are in
 * it. The order is deterministic: the same graph gives the same plan in every run.
 *
 * The plan does not change after it is built.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace camel::runtime {

class ExecPlan {
  public:
    /// The live nodes in the order they run (every node after its inputs; an arm of a branch is
    /// one range between the BRCH and its JOIN).
    std::vector<gc_node_ref_t> topo;
    /// The data slot of each node, by node reference.
    std::vector<gc_data_idx_t> dataIndexByRef;

    /// Per topo index: the callee of a FUNC node (nullptr for other nodes).
    std::vector<GCGraph *> directCallTargets;
    /// Per topo index: the FUNC node's value is the graph's result and nothing but forwarding
    /// follows it (a tail call), or it feeds a tail JOIN through forwarding.
    std::vector<uint8_t> directCallTailEligible;
    std::vector<uint8_t> directCallFeedsTailJoin;
    /// The data slots of the arguments of the FUNC node at topo index i (norm, then with):
    /// directCallArgSlots[directCallArgOffsets[i] ..< directCallArgOffsets[i + 1]].
    std::vector<uint32_t> directCallArgOffsets;
    std::vector<gc_data_idx_t> directCallArgSlots;
    /// For the BRCH at topo index i, branchArmEntries[branchArmEntryOffsets[i] + arm] is the node
    /// where that arm starts executing: its first executable node in `topo`, or the JOIN when
    /// the arm has nothing to execute.
    std::vector<uint32_t> branchArmEntryOffsets;
    std::vector<gc_node_ref_t> branchArmEntries;
    /// The node whose value is the result of the graph (see resolveRuntimeTailValueRef); invalid
    /// when the graph returns nothing or the value is not a slot.
    gc_node_ref_t tailValueRef = kInvalidNodeRef;
    size_t tailValueTopoIndex  = 0;
    bool tailValueIsJoin       = false;
    /// The data slots of the ports a caller binds, norm ports then with ports.
    std::vector<gc_data_idx_t> calleePortSlots;

    std::span<const gc_data_idx_t> directCallArgSlotsOf(size_t topoIndex) const {
        return {
            directCallArgSlots.data() + directCallArgOffsets[topoIndex],
            directCallArgOffsets[topoIndex + 1] - directCallArgOffsets[topoIndex]};
    }
    std::span<const gc_node_ref_t> branchArmEntriesOf(size_t topoIndex) const {
        return {
            branchArmEntries.data() + branchArmEntryOffsets[topoIndex],
            branchArmEntryOffsets[topoIndex + 1] - branchArmEntryOffsets[topoIndex]};
    }
};

/// Computes the plan of `graph` (a frozen graph with nodes). Use GCGraph::execPlan, which keeps
/// the result.
std::unique_ptr<ExecPlan> buildExecPlan(const GCGraph &graph);

} // namespace camel::runtime
