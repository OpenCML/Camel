// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `core.controlRegions`: the control regions of a graph as a tree -- the arms of its branches, the
 * condition and the body of each of its loops -- with the questions the validator and the back
 * ends ask about it answered in constant time: which control region a node is in, whether one
 * region lies in another, which region is an arm of which BRCH, and the position of a region in a
 * walk of the tree. Regions that only annotate (Inline, Block) are looked through.
 *
 * The tree is a fact of the graph, not a structure a reader builds for itself: it is a reading of
 * the view's region table and of nothing else, so there is one builder (`buildControlRegionTree`),
 * one value, and whoever asks second gets the tree the first asker caused. A reader that built its
 * own copy would be a second answer to the same question with its own lifetime, which is what
 * `camel/runtime/control_regions.h`'s thin reader exists to avoid.
 *
 * The kind is Graph-scoped and states no merge: a tree is a whole-graph answer with no law that
 * makes the trees of two parts one tree (facts.h, "Merging and monotonicity"). It reports the
 * regions it read, and a region's record is what the tree is made of.
 *
 * A frozen graph's content never changes, so a stored tree cannot go stale; what ends it is the
 * graph's lifetime, which the graph's owner states by calling `forgetGraph` when it frees one.
 */

#pragma once

#include "camel/facts/facts.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::facts {

/// The control regions of one graph, as one value: what `camel/runtime/control_regions.h` answers
/// its questions from, and nothing but data, so that two trees for the same graph compare equal
/// and a tree can be stored (facts.h, `Kind`).
struct ControlRegionTree {
    /// By region: the innermost control region it is in, or is (0 for a region that is in none).
    std::vector<std::uint32_t> enclosing;
    /// By control region: its position in a walk that visits a region before the regions in it,
    /// and the position after the last region in it. The body is position 0.
    std::vector<std::uint32_t> tin, last;
    /// The control regions in walk order, the body first.
    std::vector<std::uint32_t> order;
    /// The region of every arm of a BRCH, by arm index (0 where an arm has none).
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> arms;
    /// The LoopCond (0) and LoopBody (1) region of a LOOP, 0 where it has none.
    std::unordered_map<std::uint32_t, std::array<std::uint32_t, 2>> loops;

    bool operator==(const ControlRegionTree &) const = default;
};

/// The id of the kind `core.controlRegions`, registered the first time it is asked for.
KindId controlRegionKind();

/// The store the frozen graphs' facts are kept in: one store for the process's frozen graphs, keyed
/// by the graph as a value, whose entries end when their graph does (`forgetGraph`). It is how a
/// kind whose computation lives in a module reaches the store -- `std.execPlan` is one
/// (camel/runtime/exec_plan.h, `execPlanOf`) -- because a module registers and computes a kind but
/// does not own the store its answers are kept in.
Store &frozenGraphFacts();

/// The control regions of `graph`, built the first time it is asked for and kept while the graph
/// lives: the one place the tree is computed. `graph` is the graph as a value, which is what the
/// fact is anchored on.
std::shared_ptr<const ControlRegionTree> controlRegionsOf(const graph::Graph &graph);

/// Drops every fact anchored on `graph`: its owner calls this when it frees the graph, since an
/// address is only a graph while the graph is there.
void forgetGraph(const graph::Graph *graph);

/// One line of text: the number of control regions in the tree.
std::string toString(const ControlRegionTree &tree);

} // namespace camel::facts