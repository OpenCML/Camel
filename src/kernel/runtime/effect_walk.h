// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one computation of an effect summary (camel/runtime/effects.h has the value and the questions
 * it answers). These three entry points are private to the kernel's fact layer: `core_facts.cpp` is
 * the only file that includes this header, and what it does with them is register them as facts
 * (`core.effects`, `core.nodeEffects`, `core.regionEffects`). A reader asks the fact store
 * (core_facts.h) and never calls these, so there is no second reading of the same thing to keep in
 * step -- the property `docs/technical/40_graph_facts.md` wants and the one this header's placement
 * makes checkable rather than promised.
 *
 * `effectSummaryOfOperator` is not here: an operator's clause is a declaration read, which is what
 * the walk starts each node from, not a summary computed over a graph. It stays in effects.h
 * because `graph.is_pure` asks exactly that question.
 */

#pragma once

#include "camel/runtime/effects.h"

namespace camel::runtime::effectwalk {

/// The effects of running `graph`, a graph of `program`, once (nullptr: none).
EffectSummary ofGraph(const Program &program, const GCGraph *graph);
/// The effects of running the nodes of `region` of `graph` (a LoopCond or LoopBody region, the
/// regions inside it included) once.
EffectSummary ofRegion(const Program &program, const GCGraph *graph, uint32_t region);
/// The effects of running one node of `graph` of `program` once.
EffectSummary ofNode(const Program &program, const GCGraph *graph, gc_node_ref_t ref);

} // namespace camel::runtime::effectwalk