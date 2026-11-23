// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `core.arrangeable`: whether a node is freely arrangeable (design principles 3.1, 3.4) -- pure,
 * with no effect outside the program, unable to fault where it is written, and known to terminate.
 * Such a node may be moved out of a loop body or an arm, left out of a `sync` chain, dropped when
 * a rewrite stops reading it, and left without an order edge after an inline. A node that fails
 * any of the four terms keeps the place the program wrote it in.
 *
 * The four terms are one value, and the conjunction that reads them is `Arrangeable::all()`, in
 * this kind's own file: a reader that ANDs the four itself is a second statement of the answer,
 * which is the defect this kind exists to remove.
 *
 * The terms are judged **per node**, not by the function the node is written in: an OPER node is
 * known to terminate wherever it stands, a bounded `for` is, and a `while` and a call to a
 * recursive function are not. So a pure operator inside a recursive function's body is freely
 * arrangeable, and the recursive call beside it is not; that is the whole reason the termination
 * term is read at the node instead of at its enclosing graph.
 *
 * Where each term is read: the first three from the operator record of the node's operator
 * (`camel/ops/operator_record.h`: `pure` is the empty `uses` clause, `external` is the reach it
 * states, `panic` is the fault fact), and from the graph the node runs when it runs one (a branch's
 * arms, a loop's regions, a callee's body); the fourth from the node's own form
 * (`camel/core/loop_kind.h` and `CalleeRef`), never from what encloses it. A record that does not
 * state `uses` -- its silence is the reading design principles 3.3 gives the *effect* of an
 * operator, and a whole operator surface reads that way today -- is answered from the semantics the
 * operator was declared with, so that this kind answers what the tree answers; the operator record
 * and the graph, and nothing else, are what this file reads either way.
 */

#pragma once

#include "camel/facts/facts.h"

namespace camel::facts {

/// The four terms of design principles 3.4 at one node. `all()` is the conjunction -- the answer
/// to "may a pass arrange this node anywhere?".
struct Arrangeable {
    /// The node writes nothing: no variable cell, no object, no resource outside the program.
    bool pure = false;
    /// Nothing outside the program sees it run (design principles 3.3: `external`).
    bool noExternalEffect = false;
    /// Running it cannot raise a fault where the program wrote it (design principles 3.9).
    bool cannotFault = false;
    /// Running it is known to end (design principles 3.4, 3.10).
    bool terminates = false;

    /// The conjunction: what every reader of this fact asks.
    bool all() const { return pure && noExternalEffect && cannotFault && terminates; }
    bool operator==(const Arrangeable &) const = default;
};

/// The id of the kind `core.arrangeable`, registered the first time it is asked for.
KindId arrangeableKind();

/// `core.arrangeable` at an OPER node of `oper`: the four terms with the termination one answered
/// by the node's form (an operator node computes a value and hands it on). A reader that has only
/// an operator -- the chain predicate of a graph, a listing -- asks this.
Arrangeable operatorArrangeable(camel::ops::OperId oper);

/// `core.arrangeable` at any node of `graph`'s view, reading a callee's graph through `graphs`. A
/// branch, a loop and a call stand for what runs inside them: their terms are the conjunction over
/// the nodes of their arms, of their regions, or of the callee's body, and a callee already being
/// judged above this node is a recursion, which is not known to terminate. `graph` is the anchor's
/// identity, and it is what makes the recursion visible: a call back to it is one.
Arrangeable nodeArrangeable(
    const graph::Graph &graph, const graph::GraphView &view, NodeId node, const GraphAccess &graphs,
    ReadLog &log);

/// The four terms as one line: "freely arrangeable", or what fails.
std::string toString(const Arrangeable &arrangeable);

} // namespace camel::facts
