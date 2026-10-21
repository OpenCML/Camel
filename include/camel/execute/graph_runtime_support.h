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
 * Created: Apr. 06, 2026
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
 * while it ran). A frozen graph names the graphs it calls by id; the back end resolves them in
 * the program that holds the graph version, found here.
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
    /// The program that holds this version of `graph`.
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
    const camel::runtime::Program *main_    = nullptr;
    mutable std::vector<const camel::runtime::Program *> others_;
    mutable std::mutex mutex_;
};

/// A run of `main` in `context`, ended when the scope is.
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

struct RuntimeBranchArmRegion {
    camel::runtime::gc_node_ref_t headIndex = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t tailIndex = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t joinIndex = camel::runtime::kInvalidNodeRef;
    std::vector<camel::runtime::gc_node_ref_t> topoIndices;
};

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

void fillFrameForDirectInvoke(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

void fillFrameForIndirectCall(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

} // namespace camel::execute
