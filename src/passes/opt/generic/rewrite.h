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
#include <vector>
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
 * The ordered nodes `id`'s value is computed from: walking up its value inputs, and the control
 * inputs of pure nodes on the way, the first nodes that carry ordering (calls, branches,
 * effects, gates that wait). Code reading `id` runs after them because of the data dependency
 * alone, which replacing `id` by a constant, or inlining a call that ignores an argument,
 * removes.
 */
std::vector<gc_node_ref_t> orderedSources(const GraphDraft &draft, gc_node_ref_t id);

/**
 * Replaces node `id` by `replacement` (or just removes it when `replacement` is invalid, which
 * requires `id` to have no value users) and erases it. Ordering is preserved: every user of
 * `id`, value or control, inherits `id`'s control predecessors, because a pure node on a
 * control chain still orders its dependents after the effects before it, and the ordered nodes
 * `id`'s value was computed from (calls, effects, gates that wait), because reading a value
 * orders code after its producer and a constant replacing it would not. This cannot create a
 * cycle (predecessors run before `id`, which runs before its users). The caller must ensure
 * `replacement` is not downstream of `id` (see reaches()).
 */
void replaceNode(GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t replacement);

/**
 * Runs `rewrite` on the draft of every graph reachable from `graph` and commits when anything
 * changed. `rewrite` returns the number of rewrites it made (their total is stored in `rewrites`
 * when given). Returns the graph to continue with.
 */
camel::runtime::GCGraph *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::string_view passName, const std::function<size_t(GraphDraft &)> &rewrite,
    size_t *rewrites = nullptr);

} // namespace camel::passes::generic
