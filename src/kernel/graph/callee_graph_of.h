// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * How an adapter names the graph a call refers to by value (`CalleeRef`, graph_view.h).
 *
 * Neither representation stores that pointer. A frozen graph and a draft both name a callee by
 * `GraphId` (`GCGraph::calleeIdOf`, `GraphDraft::calleeIdOf`), and the id is resolved by whoever
 * owns the set of graphs: `Program` today (`Program::graph(GraphId)`), and a build's own table of
 * drafts on the other side. So the pointer comes from the owner and an adapter only translates it.
 *
 * The one cast is written where the answer is built, and it is safe there for one reason worth
 * naming: the address it casts came out of the owner's table, where every entry is one of the two
 * representations, and each of them derives from `camel::graph::Graph`
 * (`kernel/graph/include/camel/graph/graph.h`). `static_cast` is the cast that says exactly that --
 * it adjusts for the base and refuses a type that is not one.
 *
 * An adapter built without one is not wrong, it is narrower: it cannot answer a callee by value,
 * and `calleeOf` refuses there instead of answering "no callee" (a reader would take the
 * "this node calls nothing" path, and the graph would say something it does not).
 */

#pragma once

#include "camel/graph/graph.h"
#include "camel/runtime/graph_id.h"

#include <functional>

namespace camel::graph {

/// The owner's answer: the graph `id` names, or nullptr when the owner holds none. A caller wraps
/// its owner, and the wrap is the one place a representation becomes a `Graph *`:
///
///     [&program](camel::runtime::GraphId id) {
///         return static_cast<const camel::graph::Graph *>(program.graph(id));
///     }
using CalleeGraphOf = std::function<const Graph *(camel::runtime::GraphId)>;

} // namespace camel::graph