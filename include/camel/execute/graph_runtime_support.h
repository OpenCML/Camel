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
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"

#include <vector>

namespace camel::execute {

struct RuntimeBranchArmRegion {
    camel::runtime::gc_node_ref_t headIndex = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t tailIndex = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t joinIndex = camel::runtime::kInvalidNodeRef;
    std::vector<camel::runtime::gc_node_ref_t> topoIndices;
};

std::vector<camel::runtime::gc_node_ref_t>
buildReachableExecutionTopoIndices(camel::runtime::GCGraph *graph);
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
RuntimeBranchArmRegion collectRuntimeBranchArmRegion(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchIndex, size_t armIndex);
camel::runtime::gc_node_ref_t resolveRuntimeBranchArmEntry(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef, size_t armIndex,
    std::span<const camel::runtime::gc_node_ref_t> topoOrder);
size_t selectRuntimeBranchArm(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef,
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

/// True when `graph` declares a `var` parameter (norm or with).
bool runtimeGraphHasVarParams(const camel::runtime::GCGraph *graph);

/**
 * `var` parameters are passed by reference: when a call returns, the value each `var`
 * parameter holds in the callee frame `callee` is stored back into the slot of the argument the
 * FUNC or CALL node `callNodeRef` of `callerGraph` passed for it. Schedulers call this after the
 * callee ran and before its frame is released. The callee frame must be the one the arguments
 * were bound into, so a graph with `var` parameters is neither entered nor left by a tail call.
 * An argument that is a static constant is not written (constants are shared by every run of
 * the caller graph).
 */
void writeBackVarArgs(
    camel::core::context::Frame *caller, camel::core::context::Frame *callee,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

} // namespace camel::execute
