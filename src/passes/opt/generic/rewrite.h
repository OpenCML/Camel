// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

/// The integer neutral elements (x + 0, 0 + x, x - 0, x * 1, 1 * x), the implementation of
/// `std::opt::algebra neutral_only=true`. The algebraic laws themselves are the rules library's
/// (`rules::simplify`, registered as `std::opt::algebra`); this residual exists only because the
/// library has no neutral-only mode, and does nothing without `neutralOnly`.
size_t algebraDraft(GraphDraft &draft, bool safeFp = false, bool neutralOnly = false);

/// Bypass a non-anchor forwarding gate by putting its waits on mandatory consumers.
/// Conditional value consumers and costly shared barriers stay gates; edgeBudget bounds growth.
bool bypassValueGate(GraphDraft &draft, gc_node_ref_t id, size_t &edgeBudget);

/// URI of an OPER node, or an empty view for other nodes. For messages: passes recognise an
/// operator by operIdOf.
std::string_view operUriOf(const GraphDraft &draft, gc_node_ref_t id);

/// The operator an OPER node calls (camel/core/operator_table.h), or kNoOper for other nodes.
camel::core::OperId operIdOf(const GraphDraft &draft, gc_node_ref_t id);

/// True for an OPER node a pass may compute ahead of time or merge with an identical call: its
/// operator is pure and foldable. Whether the node may also be dropped, moved or reordered is a
/// different question, which only `nodeIsFreelySchedulable` (camel/runtime/graph.h) answers.
bool isPureOper(const GraphDraft &draft, gc_node_ref_t id);

/// True for nodes whose only observable result is their value, so that a pass may arrange them
/// anywhere: static DATA and the COPY/ACCS/FILL/CAST/OPER nodes that are freely schedulable (pure
/// and unable to fault).
bool isValueOnly(const GraphDraft &draft, gc_node_ref_t id);

/// True for an unread node that may be removed: a value-only node, or a pure computation that
/// may fault and sits on no chain -- nothing waits for it and it waits for nothing -- which never
/// ran (design principles 3.1: a node runs if and only if this run uses it). A pure node that may
/// fault and is on a `sync` chain stays: its fault is observable (3.4).
bool isDeadWhenUnread(const GraphDraft &draft, gc_node_ref_t id);

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
 * True when nothing the value of `id` is computed from reads a slot the program writes: walking up
 * its value inputs -- and the waits of the nodes on the way, which is what orders a computation
 * inside a `sync` block -- the walk meets no node written in place. Replacing such a computation by
 * one value gives every reader the value of a single read of that slot.
 */
bool readsNoWrittenSlot(const GraphDraft &draft, gc_node_ref_t id);

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
bool mayReduceOrder(const camel::runtime::GCGraph &graph);

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
    bool (*mayRewrite)(const camel::runtime::GCGraph &) = nullptr, bool checkScreens = false);

} // namespace camel::passes::generic
