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
 * Updated: Apr. 10, 2026
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

#include "camel/compile/gir.h"
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

std::vector<GIR::Node *> buildReachableExecutionTopo(GIR::Graph *graph);
std::vector<camel::runtime::gc_node_ref_t>
buildReachableExecutionTopoIndices(camel::runtime::GCGraph *graph);
RuntimeBranchArmRegion collectRuntimeBranchArmRegion(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchIndex, size_t armIndex);
slot_t readRuntimeGraphReturn(camel::runtime::GCGraph *graph, camel::core::context::Frame *frame);

void fillFrameForDirectInvoke(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

void fillFrameForIndirectCall(
    camel::core::context::Frame *from, camel::core::context::Frame *dest,
    camel::runtime::GCGraph *callerGraph, camel::runtime::gc_node_ref_t callNodeRef);

void fillFrameForDirectInvoke(
    camel::core::context::Frame *from, camel::core::context::Frame *dest, GIR::Graph *graph,
    GIR::Node *node);

void fillFrameForIndirectCall(
    camel::core::context::Frame *from, camel::core::context::Frame *dest, GIR::Graph *graph,
    GIR::CallNode *node);

} // namespace camel::execute
