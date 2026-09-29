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
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Draft-editing helpers shared by the generic optimization passes.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/draft.h"

#include <functional>
#include <string_view>

namespace camel::passes::generic {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphDraft;

/// URI of an OPER node, or an empty view for other nodes.
std::string_view operUriOf(const GraphDraft &draft, gc_node_ref_t id);

/// True for an OPER node whose operator is registered as pure.
bool isPureOper(const GraphDraft &draft, gc_node_ref_t id);

/// True for nodes whose only observable result is their value: pure OPERs, static DATA, and
/// CAST/COPY/ACCS/FILL.
bool isValueOnly(const GraphDraft &draft, gc_node_ref_t id);

/// True unless `id` anchors the graph (exit, output, return, or a branch-arm head/tail).
bool isReplaceable(const GraphDraft &draft, gc_node_ref_t id);

/// True when `to` is reachable from `from` along value or control edges (to is downstream).
bool reaches(const GraphDraft &draft, gc_node_ref_t from, gc_node_ref_t to);

/**
 * Replaces node `id` by `replacement` (or just removes it when `replacement` is invalid, which
 * requires `id` to have no value users) and erases it. Ordering is preserved: every user of
 * `id`, value or control, inherits `id`'s control predecessors, because a pure node on a
 * control chain still orders its dependents after the effects before it. This cannot create a
 * cycle (predecessors run before `id`, which runs before its users). The caller must ensure
 * `replacement` is not downstream of `id` (see reaches()).
 */
void replaceNode(GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t replacement);

/**
 * Runs `rewrite` on the draft of every graph reachable from `graph` and commits when anything
 * changed. `rewrite` returns the number of rewrites it made. Returns the graph to continue with.
 */
camel::runtime::GCGraph *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::string_view passName, const std::function<size_t(GraphDraft &)> &rewrite);

} // namespace camel::passes::generic
