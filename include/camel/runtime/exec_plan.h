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
 * The plan does not change after it is built, except for the record FastVM keeps in it about the
 * code it made for the version (`fastVm`).
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/runtime/graph.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace camel::runtime {

/// A node of the plan with what a back end reads of it while running, resolved once: the record,
/// the input runs, the payload and the arms of a BRCH. They point into the frozen graph, which
/// does not move, so running a node does not look anything up in the graph.
struct ExecNode {
    gc_node_ref_t ref  = kInvalidNodeRef;
    const GCNode *node = nullptr;
    const gc_node_ref_t *norm = nullptr;
    const gc_node_ref_t *with = nullptr;
    const std::byte *body     = nullptr; // the payload; null when the node has none
    const GCBranchArm *arms   = nullptr; // BRCH only
    uint32_t normCount = 0;
    uint32_t withCount = 0;
    uint32_t armCount  = 0;

    std::span<const gc_node_ref_t> normInputs() const { return {norm, normCount}; }
    std::span<const gc_node_ref_t> withInputs() const { return {with, withCount}; }
    std::span<const GCBranchArm> branchArms() const { return {arms, armCount}; }
    template <typename Body> const Body *bodyAs() const {
        return reinterpret_cast<const Body *>(body);
    }
};

class ExecPlan {
  public:
    /// The live nodes in the order they run (every node after its inputs; an arm of a branch is
    /// one range between the BRCH and its JOIN).
    std::vector<gc_node_ref_t> topo;
    /// The nodes of `topo`, in the same order.
    std::vector<ExecNode> nodes;
    /// The position in `topo` of each node, by node reference; kNotInPlan for a node that does
    /// not run (dead).
    static constexpr uint32_t kNotInPlan = UINT32_MAX;
    std::vector<uint32_t> topoIndexByRef;
    /// The data slot of each node, by node reference.
    std::vector<gc_data_idx_t> dataIndexByRef;

    /// Per topo index: the callee of a FUNC node, by id (kInvalidGraphId for other nodes). The
    /// version of the callee is the one the program the graph is run in holds
    /// (Program::graph): a version shared by several programs has callees that differ.
    std::vector<GraphId> directCallTargetIds;
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
    /// The tail value is the last thing the graph does but forwarding GATEs: a call that is the
    /// tail value, or feeds a tail JOIN through forwarding, replaces the rest of the graph (a
    /// tail call). This is the one rule every back end uses.
    bool tailAnchorOk = false;
    /// The FUNC nodes in tail position (by node reference, sorted): the tail value itself and the
    /// calls that feed a tail JOIN.
    std::vector<gc_node_ref_t> tailCalls;
    /// The JOIN nodes in tail position: the tail value when it is a JOIN, and every JOIN whose
    /// value it yields through forwarding GATEs.
    std::vector<gc_node_ref_t> tailJoins;
    bool isTailCall(gc_node_ref_t ref) const {
        return std::binary_search(tailCalls.begin(), tailCalls.end(), ref);
    }
    /// The data slots of the ports a caller binds, norm ports then with ports.
    std::vector<gc_data_idx_t> calleePortSlots;

    /// The function of each OPER node, by node reference, once a back end has resolved it; null
    /// until then (resolving an operator can import a module, and an unknown one is a fault of
    /// the run, not of the plan). Written by the back ends that run the graph node by node.
    mutable std::unique_ptr<std::atomic<operator_t>[]> resolvedOpers;
    /// What FastVM records about the code it made for this version: where its bytecode starts and
    /// how long it is (each stored plus one; 0 is "not compiled"), the JIT entry, flags, and how
    /// often an indirect call from it was made. Only FastVM reads and writes it.
    struct FastVmState {
        uintptr_t entryPc          = 0;
        uintptr_t graphLength      = 0;
        uintptr_t jitEntry         = 0;
        uintptr_t flags            = 0;
        uintptr_t indirectCallCount = 0;
    };
    mutable FastVmState fastVm;

    std::span<const gc_data_idx_t> directCallArgSlotsOf(size_t topoIndex) const {
        return {
            directCallArgSlots.data() + directCallArgOffsets[topoIndex],
            directCallArgOffsets[topoIndex + 1] - directCallArgOffsets[topoIndex]};
    }
    /// Where each arm of the BRCH `brch` starts executing (see branchArmEntries).
    std::span<const gc_node_ref_t> branchArmEntriesOfNode(gc_node_ref_t brch) const {
        return branchArmEntriesOf(topoIndexByRef[brch]);
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
