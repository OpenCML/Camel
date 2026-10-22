// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Bounded analysis and reduction of unconditional control dependencies. */

#pragma once

#include "camel/runtime/draft.h"

namespace camel::runtime {

/// Proves an existing CTRL-only path from predecessor to user, without crossing BRCH/JOIN.
/// Exhausting maxWork means "not proven", so callers must retain the edge.
bool hasControlDependency(
    const GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t maxWork = 64);

/// Retains a sufficient frontier of predecessors before a rewrite transfers their ordering.
/// Uses one shared search budget for the entire list and preserves uncertain dependencies.
std::vector<gc_node_ref_t> controlFrontier(
    const GraphDraft &draft, std::span<const gc_node_ref_t> inputs, size_t maxWork = 128);

/// Avoids direct value-covered edges and edges already implied by CTRL paths (bounded search).
/// Keeps explicit argument ordering on FUNC/CALL for later splicing, and a GATE's first CTRL.
bool appendControlDependency(
    GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t maxWork = 32);

/// Removes CTRL edges implied by mandatory data/control paths. Branch boundaries and arm
/// anchors are retained; JOIN/LOOP results may prove completion but their interiors are not
/// traversed. Work is limited per target, per graph size, and by maxWork; zero disables analysis.
size_t reduceControlDependencies(GraphDraft &draft, size_t maxWork = 250000);

} // namespace camel::runtime
