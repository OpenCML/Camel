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
 * Updated: Apr. 11, 2026
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

void fillFrameForDirectInvoke(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

void fillFrameForIndirectCall(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

} // namespace camel::execute
