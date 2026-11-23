// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The consistency check of the view: one graph, read twice through `GraphView` -- once as the draft
 * a session built and once as the frozen graph it was encoded to -- and the two answers have to be
 * the same answer.
 *
 * This is the guarantee that "every predicate is written once" is worth anything: a
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
 * What it cannot compare, and why -- each one is a field the face left off on purpose, so the check
 * not naming it is the design, not a gap in the check:
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
 *  - the order preds of a node the encoding gives no order edge: an order edge says "run before me
 *    when both run", and a node that arranges itself freely says nothing with one, so the encoder
 *    drops every order edge into it (`nodeTakesNoOrder`, camel/runtime/graph.h) while the draft
 *    keeps the edge it was built with. The two readings are both right about what they hold, and
 *    the rule that made them differ is the encoder's, so the caller says which nodes those are
 *    (`orderDropped`) and the check compares the other nodes' order preds.
 */

#pragma once

#include "graph/graph_view.h"

#include <functional>
#include <string>

namespace camel::graph {

/// Whether the encoding gives `ref` (a node of the frozen reading) no order edge at all: the
/// encoder's own rule (`nodeTakesNoOrder`, camel/runtime/graph.h), asked by whoever holds the frozen
/// graph. A node this answers true for is not compared on its order preds -- see the file comment.
using OrderDropped = std::function<bool(NodeRef)>;

/// The first difference between two readings of one graph, in the order the checks run, or an empty
/// string when they agree. `draft` is the graph the session built and `frozen` the graph it was
/// encoded to; every line names which side said what, so a difference is a reading and not a
/// verdict. `orderDropped` names the nodes whose order preds the encoding drops by rule; without it,
/// every node is compared, which is what a reading of two graphs one of which was not encoded to the
/// other wants.
std::string firstDifference(
    const GraphView &draft, const GraphView &frozen, const OrderDropped &orderDropped = {});

/// One graph read through the view, for a diagnostic: the node slots, the kinds in node order and
/// the number of regions. What a `GUARD` prints beside a difference so that the report names the
/// graph, not only the field.
std::string describe(const GraphView &graph);

} // namespace camel::graph