// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared runtime-facing GIR execution helpers.
 *
 * These routines capture graph traversal and frame binding rules that are
 * semantically identical across execution-oriented passes such as NodeVM and
 * std::macro. Policy differences such as side-effect admission, failure model,
 * and result materialization remain in the caller.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace camel::core::context {
class Context;
}

namespace camel::execute {

/**
 * The programs a back end runs graphs of: the one it was given and those of the function
 * values it meets (a function made in another program, such as a gradient the program built
 * while it ran). A frozen graph names the graphs it calls by id, and those ids mean the table
 * of one program: the one that carries the graph. A caller that has the function value passes
 * `function->program()` to call() and that is the answer (the value names its program, and
 * `graphOf` returns the version that program holds); `of` is what is left for a graph no value
 * names -- a root the run was handed -- and it answers with the program the run was given when
 * that program holds the version.
 */
class RunPrograms {
  public:
    RunPrograms() = default;
    ~RunPrograms() { end(); }
    RunPrograms(const RunPrograms &)            = delete;
    RunPrograms &operator=(const RunPrograms &) = delete;

    /// Starts running `main` in `context`: the program is rooted, and so is every program a
    /// function value brings in (note), until end(); the context does not collect the graphs
    /// itself meanwhile (the collector of run-time values does, through the heap).
    void begin(camel::core::context::Context &context, const camel::runtime::Program *main);
    void end();
    const camel::runtime::Program *main() const { return main_; }

    /// The graph of `function`, whose program is noted.
    template <typename Func> camel::runtime::GCGraph *graphOf(const Func *function) {
        note(function->program());
        return function->graph();
    }
    void note(const camel::runtime::Program *program);
    /// The program that holds this version of `graph`. A pass that has the function value in
    /// hand must hand `function->program()` to call() instead: the value names the program it
    /// was made in, while this can only look for a program that holds the version -- and a
    /// version reached from a run-time table (the lambda a closure makes, a gradient) is held by
    /// more than one program, so "who holds it" is not the same question as "whose table are
    /// these ids read in". See the file comment.
    const camel::runtime::Program &of(const camel::runtime::GCGraph *graph) const {
        if (main_ != nullptr && main_->contains(graph)) {
            return *main_;
        }
        std::lock_guard lock(mutex_);
        for (const camel::runtime::Program *program : others_) {
            // A program the collector freed since it was noted holds nothing that runs.
            if (camel::core::mm::graphSpace().isAllocated(
                    const_cast<camel::runtime::Program *>(program)) &&
                program->contains(graph)) {
                return *program;
            }
        }
        throw std::logic_error(
            "A graph being run belongs to none of the programs the back end knows.");
    }

  private:
    camel::core::context::Context *context_ = nullptr;
    bool active_                            = false;
    // program-holder-exempt: kept-by-another-root -- rootForRun pins it from begin() to end(),
    // which is exactly the life of this holder.
    const camel::runtime::Program *main_    = nullptr;
    // program-holder-exempt: kept-by-another-root -- the frames' function values hold these,
    // and a pin would keep every gradient a long loop builds (graph_space).
    mutable std::vector<const camel::runtime::Program *> others_;
    mutable std::mutex mutex_;
};

/// A run of `main` in `context`, ended when the scope is.
///
/// The scope a **back end** opens for the graph it is running. The other side of the same rule is
/// the holder's: a C++ frame that keeps a program and the value slots a call handed it must be
/// inside a run for as long as it keeps them (ProgramRunScope), and a back end that runs inside
/// that frame nests its own run there. The run is a counter and the program roots are a multiset
/// (GCGraphManager::enterRun / leaveRun / rootForRun), so neither side has to know the other's.
class RunScope {
  public:
    RunScope(
        RunPrograms &programs, camel::core::context::Context &context,
        const camel::runtime::Program *main)
        : programs_(programs) {
        programs_.begin(context, main);
    }
    ~RunScope() { programs_.end(); }
    RunScope(const RunScope &)            = delete;
    RunScope &operator=(const RunScope &) = delete;

  private:
    RunPrograms &programs_;
};

/// A run a **holder in C++** opens for itself: it keeps `program` and the value slots a call
/// hands back while those slots are live in C++, so the program is rooted for the whole window
/// (ProgramHold) and the explicit collection is off for it (the run counter). The collector moves
/// a program's static values when it collects, and a `slot_t` in a C++ local is not a root: it is
/// only valid while a run is open and nothing may collect -- which is why the window belongs to
/// whoever holds the slots, and not to the back end that ran the graph inside it.
class ProgramRunScope {
  public:
    ProgramRunScope(
        camel::core::context::Context &context, const camel::runtime::Program *program);
    ~ProgramRunScope();
    ProgramRunScope(const ProgramRunScope &)            = delete;
    ProgramRunScope &operator=(const ProgramRunScope &) = delete;

  private:
    camel::core::context::Context *context_;
    camel::runtime::ProgramHold hold_;
};

struct RuntimeBranchArmRegion {
    camel::runtime::gc_node_ref_t tailIndex = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t joinIndex = camel::runtime::kInvalidNodeRef;
    std::vector<camel::runtime::gc_node_ref_t> topoIndices;
};

/// One node another must run after, with the edge kind that says so (docs/design/effects/
/// edges.cn.md 1: a data edge, a sync edge, an order edge; a control input is the chain wait a
/// GATE carries).
struct ExecutionPredecessor {
    camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
    enum class Kind : uint8_t {
        Control, // a control input (the chain wait a GATE carries)
        Data,    // a value port: one normal-role port, or a context (`with`) one
        Sync,    // the statement written before this one in the same block
        Order,   // an edge that only orders: both nodes run, the predecessor runs first
    } kind = Kind::Data;
    bool operator==(const ExecutionPredecessor &) const = default;
};

/// The nodes `ref` must run after, and nothing else: the one answer to "what does this node wait
/// for" (docs/design/effects/edges.cn.md 2: the order obeys all three edge kinds). In this order --
/// control inputs, normal-role data ports, context (`with`) ports, sync predecessors, order
/// predecessors -- which is the order `computeExecutionOrder` walks them in, so a back end that
/// reads this list lays a graph out exactly as that walk does.
///
/// A node is named once for every edge that reaches `ref` (a value read by two ports is named
/// twice); nothing is filtered. The refs are the stored ones, so a caller laying out only the
/// nodes that run skips the ones its own liveness does not hold -- an order edge orders two nodes
/// that both run and never makes either of them run.
///
/// This is the single implementation. The execution order below, the parallel scheduler's task
/// graph (`src/passes/sched/taskflow`), and the storage-reuse proof of the plan
/// (`src/execute/exec_plan.cpp`) all read it, so no pass keeps a list of its own. It reads the
/// graph and runs nothing, so a test may call it on its own.
std::vector<ExecutionPredecessor> executionPredecessorsOf(
    const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref);

/// Computes the order the live nodes of `graph` run in. Back ends do not call it: they read the
/// execution plan (camel/runtime/exec_plan.h), which calls it once per graph version.
std::vector<camel::runtime::gc_node_ref_t>
computeExecutionOrder(const camel::runtime::GCGraph *graph);
camel::runtime::gc_node_ref_t
resolveRuntimeForwardedValueRef(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t ref);
camel::runtime::gc_node_ref_t resolveRuntimeTailValueRef(camel::runtime::GCGraph *graph);
bool runtimeNodeOutputsContain(
    const camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
    camel::runtime::gc_node_ref_t targetRef);
// True when nothing of the arm runs after `valueRef` before the arm ends at `armTailRef`: the
// tail reaches the value through forwarding GATEs that wait only on the value, on each other,
// or on nodes that do not themselves run after the value. A GATE on that path that waits on an
// effect ordered after the value (`let r = f(x); println(..); return r`, inlined into an arm)
// makes this false: the value is then not the last thing the arm does.
bool runtimeArmEndsWithValue(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t armTailRef,
    camel::runtime::gc_node_ref_t valueRef);
// The values a tail JOIN can yield, seen through forwarding GATEs: the arm values of the
// JOIN and, recursively, of every JOIN among them (a nested branch in tail position). An arm
// whose value is not the last thing it does (runtimeArmEndsWithValue) contributes nothing.
std::vector<camel::runtime::gc_node_ref_t> collectRuntimeTailJoinArmValues(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t tailJoinRef);
bool hasOnlyTrivialRuntimeTailSuffixAfter(
    const camel::runtime::GCGraph *graph, std::span<const camel::runtime::gc_node_ref_t> topoOrder,
    size_t anchorIndex);
/// The nodes of the arm `armIndex` of the BRCH `brchIndex`: those of its region, in the order they
/// depend on one another (the region of the arm names them; see GCRegion).
RuntimeBranchArmRegion collectRuntimeBranchArmRegion(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchIndex, size_t armIndex);
/// Where each arm of each BRCH of `graph` starts in the execution order `topoOrder`: the first node
/// of the arm region that runs something, the JOIN when none does.
class BranchArmEntries {
  public:
    BranchArmEntries(
        const camel::runtime::GCGraph &graph,
        std::span<const camel::runtime::gc_node_ref_t> topoOrder);
    camel::runtime::gc_node_ref_t entry(camel::runtime::gc_node_ref_t brch, size_t arm) const {
        return entries_.at(brch)[arm];
    }
    /// The last node of the arm in the order; the JOIN when the arm has none.
    camel::runtime::gc_node_ref_t tail(camel::runtime::gc_node_ref_t brch, size_t arm) const {
        return tails_.at(brch)[arm];
    }

  private:
    std::unordered_map<camel::runtime::gc_node_ref_t, std::vector<camel::runtime::gc_node_ref_t>>
        entries_;
    std::unordered_map<camel::runtime::gc_node_ref_t, std::vector<camel::runtime::gc_node_ref_t>>
        tails_;
};
size_t selectRuntimeBranchArm(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef,
    camel::core::context::Frame *frame);
/// The same for a BRCH of the execution plan, without looking anything up in the graph.
size_t selectRuntimeBranchArm(
    const camel::runtime::ExecPlan &plan, const camel::runtime::ExecNode &brch,
    camel::core::context::Frame *frame);
slot_t readRuntimeGraphReturn(camel::runtime::GCGraph *graph, camel::core::context::Frame *frame);

void writeRuntimeFillSlots(
    camel::core::rtdata::Object *target, camel::core::type::Type *targetType,
    const camel::runtime::GCFillBody *fillBody, std::span<const slot_t> values);

/// The value a FUNC node hands on: the callee's result alone when the callee writes no captured
/// variable (`written` is then 0), and otherwise the tuple of it and the new version of each
/// variable the callee writes, in the callee's capture order. `withValues` are the values of the
/// call's With inputs, in the order it passes them: the callee's declared `with` arguments, then
/// its captures; the versions are taken from the capture ports, which `written` names. The tuple
/// is materialized per call, typed by the call node's own value type, and the ACCS nodes of the
/// call read its parts. `result` is the callee's return value.
slot_t runtimeFuncValue(
    camel::core::type::Type *valueType, camel::runtime::GCGraph *calleeGraph, uint32_t written,
    std::span<const slot_t> withValues, slot_t result);

void fillFrameForDirectInvoke(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

void fillFrameForIndirectCall(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

} // namespace camel::execute
