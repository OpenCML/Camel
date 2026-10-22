// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::stats: graph size statistics of the program as it is at that point of
 * the pass pipeline. Prints one line
 *   GRAPH_STATS {"graphs":..,"nodes":..,"edges":..,"opers":..,"direct_calls":..,
 *                "indirect_calls":..,"closures":..,"branches":..,"loops":..,"bytes":..}
 * over every graph reachable from the root, and passes the program on
 * unchanged, so `camel f.cml std::macro std::stats std::opt std::stats`
 * compares a program before and after simplification.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <string>

struct GraphStats {
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
