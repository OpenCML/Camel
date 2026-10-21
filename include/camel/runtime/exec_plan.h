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
#include <mutex>
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
    uint32_t normCount = 0;
    uint32_t withCount = 0;
    uint32_t armCount  = 0; // BRCH only

    std::span<const gc_node_ref_t> normInputs() const { return {norm, normCount}; }
    std::span<const gc_node_ref_t> withInputs() const { return {with, withCount}; }
    template <typename Body> const Body *bodyAs() const {
        return reinterpret_cast<const Body *>(body);
    }
};

/// A LOOP node whose condition and body are regions of the graph, as a back end runs it in the
/// frame of the graph (docs/technical/39_loops.md). In `topo` the nodes of the condition are one
/// range, those of the body the next, and the LOOP node follows them: [begin, condEnd) is the
/// condition, [condEnd, position) the body. A round is the condition, the test of the round
/// (at `condEnd`), the body, and the LOOP node, which takes the result of the body as the state
/// and starts the next round at `begin`. Slots are data slots of the frame.
struct ExecLoop {
    uint32_t position        = 0; // the topo index of the LOOP node
    uint32_t begin           = 0;
    uint32_t condEnd         = 0;
    uint32_t stateCount      = 0; // k
    uint32_t collectionCount = 0; // s
    bool iterates            = false; // for, for ... while: one element a round
    bool hasCond             = false; // while, for ... while
    gc_data_idx_t stateSlot      = 0; // the slot of the LOOP node: the state, a tuple if it wraps
    gc_data_idx_t iterableSlot   = 0;
    gc_data_idx_t condResultSlot = 0;
    gc_data_idx_t bodyResultSlot = 0;
    /// The slot the rounds are counted in: the number of rounds done while the loop runs, -1 once
    /// its test has stopped it.
    gc_data_idx_t roundSlot      = 0;
    /// loopSlots[slotsOffset ..< +k] the slots of the first values of the state, then k of the
    /// condition's ports (when hasCond), then the body's ports: the element first (when it
    /// iterates), then k.
    uint32_t slotsOffset = 0;
    /// The value type of the node when it wraps its state (k > 1 or it collects); else null.
    const camel::core::type::TupleType *valueType = nullptr;
    uint64_t origin = 0;
};

/// What a back end does at a position of the plan for a loop that starts or goes on there.
enum class ExecLoopEvent : uint8_t {
    Begin, // the loop is entered: the first state; the condition's ports
    Test,  // the condition (if any) has run: stop, or bind the ports of the body
    End,   // the LOOP node: take the next state and go back to `begin`, or leave
};

class ExecPlan {
  public:
    /// The live nodes in the order they run (every node after its inputs; an arm of a branch is
    /// one range between the BRCH and its JOIN).
    std::vector<gc_node_ref_t> topo;
    /// The nodes of `topo`, in the same order. Built the first time they are asked for (only a
    /// back end that walks the nodes while it runs needs them), then kept.
    const std::vector<ExecNode> &nodes() const {
        if (nodesReady_.load(std::memory_order_acquire)) [[likely]] {
            return nodes_;
        }
        return buildNodes();
    }
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
    /// The same for where the arm ends: the last node of the arm in `topo`; the JOIN when the arm
    /// has nothing to execute.
    std::vector<gc_node_ref_t> branchArmTails;
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

    /// The loops of the graph that are regions of it. loopEventOffsets[i] ..< loopEventOffsets[i
    /// + 1] are the events at topo index i, each `loop index * 3 + ExecLoopEvent`, the outer
    /// loop's first; both are empty when the graph has no such loop.
    std::vector<ExecLoop> loops;
    std::vector<gc_data_idx_t> loopSlots;
    std::vector<uint32_t> loopEventOffsets;
    std::vector<uint32_t> loopEvents;
    std::span<const uint32_t> loopEventsAt(size_t topoIndex) const {
        return {
            loopEvents.data() + loopEventOffsets[topoIndex],
            loopEventOffsets[topoIndex + 1] - loopEventOffsets[topoIndex]};
    }

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
        /// Keeps the bytecode range of the version in the interpreter's code space; gives it
        /// back when the version is freed (the plan is freed with it).
        std::shared_ptr<void> codeLease;
    };
    mutable FastVmState fastVm;

    /// The graph version the plan was built from.
    const GCGraph *graph = nullptr;

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
    /// Where each arm of the BRCH `brch` ends (see branchArmTails).
    std::span<const gc_node_ref_t> branchArmTailsOfNode(gc_node_ref_t brch) const {
        return branchArmTailsOf(topoIndexByRef[brch]);
    }
    std::span<const gc_node_ref_t> branchArmTailsOf(size_t topoIndex) const {
        return {
            branchArmTails.data() + branchArmEntryOffsets[topoIndex],
            branchArmEntryOffsets[topoIndex + 1] - branchArmEntryOffsets[topoIndex]};
    }

  private:
    const std::vector<ExecNode> &buildNodes() const;

    mutable std::mutex nodesMutex_;
    mutable std::atomic<bool> nodesReady_{false};
    mutable std::vector<ExecNode> nodes_;
};

/// Computes the plan of `graph` (a frozen graph with nodes). Use GCGraph::execPlan, which keeps
/// the result.
std::unique_ptr<ExecPlan> buildExecPlan(const GCGraph &graph);

} // namespace camel::runtime
