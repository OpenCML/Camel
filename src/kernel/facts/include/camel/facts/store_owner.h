// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One store, and who owns it -- a draft for review, not a shape in use.
 *
 * `facts.h` says what a store holds and how a reader asks for it. It does not say who makes one,
 * what a store is anchored on, or what ends a graph's facts. The tree answers that by not
 * answering: no translation unit in production makes a `facts::Store` except
 * `facts/control_regions.cpp`, which makes one for the process's frozen graphs and hears about a
 * graph's end from `~GCGraph`. Everything else here has no instance, and a framework with no
 * instance reads the same as one that was never written.
 *
 * The declarations below write the four questions of this lane's question page, "who owns a store",
 * as the shapes that would answer them. No translation unit defines any of them and nothing
 * includes this header, so nothing here is proved; when the author rules, either these
 * declarations get definitions or the file is deleted. Nothing in this file decides an answer.
 */

#pragma once

#include "camel/facts/facts.h"

#include <cstdint>

namespace camel::facts {

/// Question 1, first part: what a store is anchored on. The three lifetimes the tree has facts
/// for today, in the words `facts.h` uses for the first two and of the one instance for the third.
enum class StoreScope : std::uint8_t {
    /// One program's frozen graphs: the store lives as long as the program that holds them.
    Program,
    /// One editing session's graph: the store dies with the session, because what the facts were
    /// computed from does not outlive it.
    Session,
    /// The process's frozen graphs, whose owners end before the process does: a graph's end is
    /// announced to the store, which keeps its other graphs' facts.
    Process,
};

/// Question 1, second part: the store a reader gets. `storeOf(graph)` is the shape the one
/// instance in the tree has -- a reader holds a graph and asks it for its facts;
/// `storeOf(scope, owner)` is the shape the other two lifetimes would need, where a reader that
/// has no graph yet (a pass about to build one, a session opening) asks the owner for the store.
/// Which of the two a reader writes, or whether both are named, is the question.
Store &storeOf(const graph::Graph &graph);
Store &storeOf(StoreScope scope, const void *owner);

/// Question 1, third part: the end of a graph, announced to the store that holds its facts. Only a
/// `Process` store needs this, since only it outlives its graphs; the alternative is a store that
/// belongs to the owner of the graphs and is ended by that owner, in which case nothing announces
/// anything. Three shapes were named: the graph announces (`graphEnded`), the owner ends the store,
/// or the store holds a record of the graphs that are still alive and answers nothing about a dead
/// one -- which is a red at the reader, not a missing answer.
void graphEnded(const graph::Graph *graph);

/// Question 2: what a check can say about a store without knowing one kind of it. A store that was
/// never asked and a store that was never made read the same today, so the first reader that proves
/// a store is a question: `computeAll` and `--facts` would be that reader, and `--facts` prints the
/// older store's facts (`runtime::fact::dumpFacts`), not this one's.
struct StoreTally {
    /// Facts the store holds now, of every kind.
    std::uint32_t held = 0;
    /// Computations run, of every kind.
    std::uint32_t computed = 0;
    /// Answers given from a fact already held.
    std::uint32_t answered = 0;
};
StoreTally tally(const Store &store);

/// Question 3: whether a kind is registered -- the question a reader asks when the compute of a
/// kind lives in a module that may not be loaded (`core.execPlan`, whose compute is `std::exec`'s
/// `buildExecPlan`, against the kernel's `GCGraph::execPlan()`). What a reader gets when the kind is
/// absent -- a throw, an empty answer, or a computation of its own -- is the question, and it is the
/// reason the execution plan is not moved yet: it is an interface question, not a move.
bool kindRegistered(const Store &store, KindId kind);

/// Question 4: nothing in a kind says whether its value is written after it is computed, and
/// `Store::obtain<T>` hands out a `shared_ptr<const T>`. The answer taken is that no fact value is
/// written at all: the execution plan is a whole immutable value (`std.execPlan`), and the state a
/// bytecode engine records about the code it made for a version stopped being part of it -- it is
/// the engine's own, keyed by the plan (`<fvm>/version_state.h`). So neither of the two names below
/// is the shape any more, and the kind they would have declared for a back end's state is not the
/// shape a fact has.
KindId frozenExecPlanKind();
KindId fastVmStateKind();

} // namespace camel::facts