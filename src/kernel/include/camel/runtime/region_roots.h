// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/*
 * Which region decides whether a node runs (design principles 3.1, 3.7; the region table of
 * camel/runtime/graph.h): the innermost arm or loop region the node sits in, and the BRCH or LOOP
 * whose running is that region's running.
 *
 * The *roots* a region has -- the nodes a running region runs although no value of theirs is
 * read -- are not derived by each walk (the warning, the liveness of a graph, the encode and the
 * execution order). Design principles 3.1 makes them an edge: at build time every node of the
 * region that writes an external resource or a handle by effect (an operator, a call, a branch or
 * a loop alike) gets a sync edge to the region's exit, whether or not its value is read, and
 * execution, cleanup and every back end only follow edges. So the use is laid once, by
 * `Builder::layRegionWriteEdges` (src/compile/gir/builder_diagnostics.cpp), from the node to the
 * exit of the region that decides it -- `RegionRecord::result`, the node whose value the region
 * yields -- and every walk follows edges from then on.
 *
 * What is left here is the question that edge needs answered: which region decides whether a node
 * runs, and which node decides whether a region runs. A node of the body is decided by the body
 * (region 0, which runs whenever the graph does); a node in an arm or a loop body is decided by the
 * innermost such region around it, and that region by its owner.
 *
 * **One implementation, on the view.** These were two implementations -- one over a frozen
 * `GCGraph` and a template over the draft a builder holds -- because the two representations store
 * their region table in two shapes. `GraphView` answers the table for both, so the walk is written
 * once and a caller passes the view of whichever representation it holds
 * (`FrozenGraphAdapter`, `DraftGraphAdapter`). A region the program cannot choose to run or not
 * -- the body, an inlined body, a tagged block, a removed region -- is transparent, and the view
 * reports those three as a body (`RegionKind::Root`), so the walk goes through them here exactly as
 * it did before.
 */

#include "graph/graph_view.h"

#include <cstdint>

namespace camel::runtime {

/// The region whose running decides whether `ref` runs: the innermost arm or loop region the node
/// sits in, and 0 -- the body -- for a node of the body and for one in a region the program cannot
/// choose to run or not. A node the graph does not have is decided by the body as well: it runs
/// nowhere.
camel::graph::RegionId decidingRegionOf(const camel::graph::GraphView &graph, camel::graph::NodeRef ref);

/// The node whose running is the running of region `region`: the BRCH or the LOOP that region is
/// a part of (`RegionRecord::owner`), and `kNoNode` for the body, which runs whenever the graph
/// does.
camel::graph::NodeRef decidingOwnerOf(const camel::graph::GraphView &graph, camel::graph::RegionId region);

} // namespace camel::runtime