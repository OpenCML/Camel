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
 * One table, made when the graph first needs one. A graph that no fact was ever written into has
 * no table at all -- not a table of empty slots, but a null pointer -- so a frozen graph nobody
 * asks a fact about pays nothing for the possibility. The table is made on the first write, with
 * one slot for each kind registered at that moment (the registry is append-only, so "registered
 * now" is exactly the set a writer can name), and it is replaced by a larger copy when a kind
 * registered later is written:
 *
 *   - the new table is filled from the old one and then published with one atomic store, so a
 *     reader sees one table whole or the other whole, never a half-written one;
 *   - the old table goes on the graph's retired list instead of being freed, and is released with
 *     the graph. A reader that loaded the old pointer before the swap is inside the graph's life --
 *     the condition every other read of a frozen graph already needs -- and it must still read a
 *     table rather than a freed one. That is the price of the lock-free read, and it is paid once
 *     per growth.
 *
 * What a reader relies on: a graph is frozen, so a fact about it cannot go stale; the only two
 * moments a slot changes are the store dropping the fact and the graph ending, and both clear the
 * slot before the value is released. A reader that has the graph is therefore inside the graph's
 * life, which is the same condition every other read of a frozen graph already needs.
 *
 * The read is `GCGraph::factSlot` and `GCGraph::nodeFactSlot`: load the table pointer, bound the
 * kind against its capacity, load the slot. A kind at or past that capacity is a kind this graph's
 * table has not grown to yet, and that -- not "the thirty-third kind" -- is what hands the read
 * back to the store: the slots are a cache on the fast path, never a bound on what can be stored
 * or answered, and the next write of that kind makes the table large enough.
 *
 * Nothing on that path grows the table. A read that finds the kind past the capacity hands the
 * question to the store and stops; the store's next write of that kind is what makes a larger
 * table, so the table a reader holds is replaced by a write and never by another read. That is why
 * the reader needs no protocol at all: replacing a table is in the writer's path, and what makes a
 * replaced table safe to read afterwards is its lifetime -- it is retired, not freed -- and not the
 * moment it was replaced. (`test::fact_slots` builds that read by hand: it keeps the table across a
 * registration and a write, then reads the kind it wrote before through the table it kept.)
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace camel::graph {
class Graph;
}

namespace camel::runtime {

/// The dense array of one Node fact kind: one slot per node id of the graph.
struct FactNodeArray {
    /// The graph's list of the arrays it made, freed with the graph.
    FactNodeArray *next = nullptr;
    /// The value of the fact of that kind at each node, or null where it has none.
    std::atomic<const void *> *slots = nullptr; // nodeCount() entries
};

/// One table of a frozen graph's fact slots, indexed by the kind's registration id: `capacity`
/// entries, one per kind registered when the table was made. An entry holds both shapes -- a Graph
/// fact's value and a Node fact's dense array -- because a kind is one or the other and one index
/// serves both.
struct FactSlotTable {
    /// How many kinds this table has a slot for. A kind at or past it has none here.
    std::uint32_t capacity = 0;
    /// The graph's retired list: a table a growth replaced, kept until the graph ends because a
    /// reader may have loaded it before the swap.
    FactSlotTable *next = nullptr;
    /// A Graph fact's value, per kind.
    std::atomic<const void *> *values = nullptr;
    /// A Node fact's dense array, per kind; null until that kind's first fact.
    std::atomic<FactNodeArray *> *nodeArrays = nullptr;
};

/// The slot `kind` has in `table`, or null: the read `GCGraph::factSlot` makes, through a table a
/// caller took earlier (`GCGraph::factSlotTable`). A table a growth replaced still answers, because
/// the graph keeps it until it ends -- which is what makes the lock-free read safe.
inline const void *factSlotIn(const FactSlotTable *table, std::uint32_t kind) noexcept {
    return table == nullptr || kind >= table->capacity
               ? nullptr
               : table->values[kind].load(std::memory_order_acquire);
}

/// Publishes (`value` not null) or clears (null) `graph`'s Graph-fact slot of `kind`. Writing a
/// slot of a graph that keeps no slots -- a draft -- does nothing.
void setFactSlotOf(const camel::graph::Graph &graph, std::uint32_t kind, const void *value) noexcept;

/// Writes one node's entry of a Node fact's dense array, making the array on the first Node fact of
/// `kind`.
void setNodeFactSlotOf(
    const camel::graph::Graph &graph, std::uint32_t kind, std::uint32_t node,
    const void *value) noexcept;

} // namespace camel::runtime