// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The consistency check of the view: one graph, read twice through `GraphView` -- once as the draft
 * a session built and once as the frozen graph it was encoded to -- and the two answers have to be
 * the same answer.
 *
 * This is the guarantee that "every predicate is written once" is worth anything (stage 2.1): a
 * query the adapter gets wrong on one side shows up here as a difference, and a query one side
 * simply cannot answer is named below rather than silently skipped. It is asked of two readers:
 *
 *  - the encoder, on every graph of every program it writes (`compile/src/gir/graph_encode.cpp`),
 *    so a program that compiles has had it asked and answered;
 *  - the test pass `test::graph_faces`, which prints the verdict, so a change in either adapter
 *    shows up as a changed line in the fast layer instead of as silence.
 *
 * What it compares is every answer of the view that is a fact about the graph: the regions and
 * their records, the answer's node, the node slots, and per node what the view says about it -- its
 * presence, region, kind, type, constant, input lists, sync and order predecessors, callee kind,
 * the regions it owns and (for a LOOP) its form.
 *
 * What it cannot compare, and why -- each one is a field the interface review of 2026-11-22 took off
 * the face on purpose, so the check not naming it is the design, not a gap in the check:
 *
 *  - the static area and a data index: which slot a value sits in belongs to the execution plan
 *    (section 13). The constants themselves are compared per node (`constantOf`), which is what a
 *    reader of the view gets.
 *  - a node's flags: macro-ness is the record of the callee or of the operator (2.2), and a
 *    constant is `constantOf`. Neither is a graph field a reader of the view sees.
 *  - whether a node carries payload bytes: no payload bytes leave the face, and the two
 *    representations keep them at different offsets by design.
 *  - a FUNC node's callee *id*: the callee is compared as the kind of reference it is
 *    (`CalleeRef`), because the two representations name it in two numberings and "the same graph"
 *    is the encoder's id table's answer, not the view's. The operator a node names *is* compared
 *    (`operatorOf`): an operator id is global, so both readings name one operator or two, and the
 *    four kinds plan 2.3 has not declared yet answer `kNoOper` on both sides, which the comparison
 *    reads as agreement rather than as a difference it cannot see.
 *  - the node the graph answers (`exit`): a draft keeps a place for a node the encoding dropped
 *    (`camel/runtime/draft.h`: an erased node keeps its place on the draft side and an encoding has
 *    no dead places), and the encoder does not write the answer's reference for a graph whose
 *    answer it wrote as dead. So the two sides can name two different answers for one graph and
 *    still be two readings of the same graph, which is why this check does not compare it --
 *    `test::graph_faces` prints both exits and pins that they agree on the graphs it runs
 *    (`[GF] exit ...`, `same=1`, and the plan `lit2.graph_faces`), which is where a change in
 *    either adapter's answer would show up.
 *  - a node's source origin: it was not compared before this stage either, and a difference there
 *    is a fact about the front end's bookkeeping, not about the two readings of one graph.
 */

#pragma once

#include "graph/graph_view.h"

#include <string>

namespace camel::graph {

/// The first difference between two readings of one graph, in the order the checks run, or an empty
/// string when they agree. `draft` is the graph the session built and `frozen` the graph it was
/// encoded to; every line names which side said what, so a difference is a reading and not a
/// verdict.
std::string firstDifference(const GraphView &draft, const GraphView &frozen);

/// One graph read through the view, for a diagnostic: the node slots, the kinds in node order and
/// the number of regions. What a `GUARD` prints beside a difference so that the report names the
/// graph, not only the field.
std::string describe(const GraphView &graph);

} // namespace camel::graph