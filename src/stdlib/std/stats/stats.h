// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::stats: graph size statistics of the program as it is at that point of
 * the pass pipeline. Prints one line
 *   GRAPH_STATS {"graphs":..,"nodes":..,"edges":..,"opers":..,"direct_calls":..,
 *                "indirect_calls":..,"closures":..,"branches":..,"loops":..,"bytes":..,
 *                "specializations":..,"cycle_dropped_sessions":..,
 *                "graph_names":[..],"macro":[..]}
 * over every graph reachable from the root, and passes the program on
 * unchanged, so `camel f.cml std::macro std::stats std::opt std::stats`
 * compares a program before and after simplification.
 *
 * `graph_names` and `macro` carry one entry per graph, in the order the walk reached the graphs --
 * the order `graphs` counts them and the order `std::jgir` numbers them (`g0`, `g1`, ...). The
 * boolean is `GCGraph::isMacro` read off the graph the walk already holds, so the line says which
 * graph is a macro graph rather than how many are; no total is printed, because it is countable
 * from the booleans.
 *
 * `cycle_dropped_sessions` is not a property of the graphs: it counts the rewrite sessions the
 * optimiser dropped because the graph they wrote closed a cycle (camel/runtime/cycle.h), so it is
 * the same number in every line of one run and 0 means no rule composed a cycle on this program.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <string>
#include <vector>

struct GraphStats {
    /// One graph of the walk: its name, and whether it answers `isMacro` (camel/runtime/graph.h).
    /// `toJson` writes the vector as the two lists `graph_names` and `macro`, field by field and in
    /// this order, so the two cannot drift apart.
    struct GraphEntry {
        std::string name;
        bool macro = false;
    };
    size_t graphs        = 0;
    size_t nodes         = 0;
    size_t edges         = 0;
    size_t opers         = 0;
    size_t directCalls   = 0;
    size_t indirectCalls = 0;
    size_t closures      = 0;
    size_t branches      = 0;
    size_t loops         = 0; // LOOP nodes
    // The graph store's bytes over the reachable graphs: node records, their input runs and
    // their payloads (camel/runtime/graph.h). A baseline for the graph-store work, not a
    // measure of the process's heap.
    size_t bytes = 0;
    // The specializations the program remembers (camel/runtime/specializations.h). The table is a
    // cache, not an owner: it lives with the program (a pass keeps the copies it made for the
    // passes that follow) but a commit drops the entries whose graph nothing reachable calls any
    // more, so an entry present here names a copy the root still reaches.
    size_t specializations = 0;
    // Rewrite sessions dropped because the graph they wrote closed a cycle (camel/runtime/cycle.h,
    // recordCycleDroppedSession). Not a property of the graphs: the program keeps the graph it had,
    // so one optimisation was skipped, and this counts how often. Kept so that a rule composing a
    // cycle shows up in a reading instead of quietly doing less work.
    size_t cycleDroppedSessions = 0;
    /// The graphs of the walk, in the order it reached them: the same order `graphs` counts.
    std::vector<GraphEntry> graphEntries;

    std::string toJson() const;
};

/// Statistics of every graph reachable from `root`.
GraphStats collectGraphStats(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *root);
/// The same for a graph of `program`.
GraphStats collectGraphStats(const camel::runtime::Program &program, camel::runtime::GCGraph *root);

class GraphStatsPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
