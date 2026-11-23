// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The slots a frozen graph keeps for the facts about it (`camel/facts/facts.h`).
 *
 * A fact is still the store's: it computes one when it is first asked, owns the value, decides
 * when it is stale, and drops it when the graph ends. What changes is how a reader that holds the
 * graph reaches the value it was answered with once before: instead of a lookup by graph and kind
 * through the store's tables, a frozen graph keeps one slot per kind -- indexed by the small
 * integer registration gave the kind, `facts::KindId` -- and the store writes the value there when
 * it stores the fact and clears the slot before it releases the value.
 *
 * Two shapes, because the two scopes ask different questions:
 *
 *   - a Graph fact's slot holds the value itself (the execution plan is one);
 *   - a Node fact's slot holds a dense array indexed by node id, so the read for node `n` is one
 *     more index. The array is made on the first Node fact of its kind on that graph, once its
 *     node count is fixed (a frozen graph never grows).
 *
 * The read is the graph's own: `GCGraph::factSlot` and `GCGraph::nodeFactSlot` are an indexed load
 * and a null check, and `GCGraph::execPlan` is the one that is on every call and every operator.
 * This header is the write half -- what the store calls -- and the one number both halves agree on.
 *
 * What a reader relies on: a graph is frozen, so a fact about it cannot go stale; the only two
 * moments a slot changes are the store dropping the fact and the graph ending, and both clear the
 * slot before the value is released. A reader that has the graph is therefore inside the graph's
 * life, which is the same condition every other read of a frozen graph already needs.
 *
 * A kind registered after the graph's slot table is full is served by the store alone: the slots
 * are a bound on the fast path, never on what can be stored or answered.
 */

#pragma once

#include <cstdint>

namespace camel::graph {
class Graph;
}

namespace camel::runtime {

/// How many kinds a graph keeps a slot for, in registration order. The first registered kinds are
/// the core's and the standard library's own; a program that registers more user facts than this
/// gets correct answers from the store and no slot.
inline constexpr std::uint32_t kFactSlotCount = 32;

/// Publishes (`value` not null) or clears (null) `graph`'s Graph-fact slot of `kind`. Writing a
/// slot of a graph that keeps no slots -- a draft -- does nothing.
void setFactSlotOf(const camel::graph::Graph &graph, std::uint32_t kind, const void *value) noexcept;

/// Writes one node's entry of a Node fact's dense array, making the array on the first Node fact of
/// `kind`.
void setNodeFactSlotOf(
    const camel::graph::Graph &graph, std::uint32_t kind, std::uint32_t node,
    const void *value) noexcept;

} // namespace camel::runtime