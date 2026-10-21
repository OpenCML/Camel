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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Draft-editing helpers shared by the generic optimization passes.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/draft.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::passes::generic {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphDraft;

/// Bounded local scalar arithmetic. safeFp restricts float rules to strict equivalence.
size_t algebraDraft(GraphDraft &draft, bool safeFp = false);
/// The same under an explicit policy (camel/core/operator_semantics.h): safeFp is the strict one.
size_t algebraDraft(GraphDraft &draft, const camel::core::sem::NumericPolicy &policy);

/// Bypass a non-anchor forwarding gate by putting its waits on mandatory consumers.
/// Conditional value consumers and costly shared barriers stay gates; edgeBudget bounds growth.
bool bypassValueGate(GraphDraft &draft, gc_node_ref_t id, size_t &edgeBudget);

/// URI of an OPER node, or an empty view for other nodes. For messages: passes recognise an
/// operator by operIdOf.
std::string_view operUriOf(const GraphDraft &draft, gc_node_ref_t id);

/// The operator an OPER node calls (camel/core/operator_table.h), or kNoOper for other nodes.
camel::core::OperId operIdOf(const GraphDraft &draft, gc_node_ref_t id);

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
 * The graphs the passes of one run found nothing to rewrite in. What a pass of this file does to
 * a graph depends on that graph alone (its nodes, constants and types; not the graphs it calls),
 * so a graph a pass left alone is left alone again by the same pass for as long as that content
 * is the same, whichever graph object holds it. Rounds of the integrated optimizer ask each pass
 * about every graph, and most graphs stop changing after the first round.
 */
class RewriteMemo {
  public:
    bool leftAlone(std::string_view passName, const camel::runtime::GCGraph *graph) const;
    void remember(std::string_view passName, const camel::runtime::GCGraph *graph);

  private:
    std::unordered_map<std::string, std::unordered_set<uint64_t>> contentsByPass_;
};

/**
 * Whether the pass named may rewrite something in a graph, read from the frozen graph. They are
 * necessary conditions, not exact ones: false means the pass would find nothing, true means it
 * might. Opening a draft of a graph costs more than looking at it, and most graphs have nothing
 * for a given pass.
 */
bool mayFold(const camel::runtime::GCGraph &graph);
bool mayMerge(const camel::runtime::GCGraph &graph);
bool mayAlgebra(const camel::runtime::GCGraph &graph);
bool mayRemoveDead(const camel::runtime::GCGraph &graph);
bool mayReduceControl(const camel::runtime::GCGraph &graph);

/**
 * Runs `rewrite` on the draft of every graph reachable from the root of `program` and commits
 * when anything changed. `rewrite` returns the number of rewrites it made (their total is stored
 * in `rewrites` when given). Returns the program to continue with. A graph for which
 * `mayRewrite` is given and false, or which the `memo` knows the pass to have left alone, is not
 * given to `rewrite`; with `checkScreens` it is, and a rewrite there is an error (a screen that
 * is too strict).
 */
const camel::runtime::Program *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::string_view passName, const std::function<size_t(GraphDraft &)> &rewrite,
    size_t *rewrites = nullptr, RewriteMemo *memo = nullptr,
    bool (*mayRewrite)(const camel::runtime::GCGraph &) = nullptr, bool checkScreens = false,
    bool allowNonExecutable = false);

} // namespace camel::passes::generic
