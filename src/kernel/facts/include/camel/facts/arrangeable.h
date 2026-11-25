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
 * states, `panic` is the fault fact), and from the graph the node runs when it runs one of this
 * graph's own regions (a branch's arms, a loop's regions); the fourth from the node's own form
 * (`camel/core/loop_kind.h`), never from what encloses it. A call is the one node this kind does
 * not answer for: the body it runs is another graph's, and `nodeArrangeable` says why walking it
 * would answer a different question.
 *
 * The fault term has one exception, and it is the node's and not the operator's: a **conversion**
 * fails or does not fail by the pair of types it converts between (`runtime::castMayFault`), and the
 * operator's record can only say the half that is true of every cast -- that a cast may fault. So at
 * an OPER node whose operator is a conversion, `nodeArrangeable` asks the kernel's one reader of that
 * pair (`runtime::nodeMayFault`, the function the predicate a pass asks is built on) instead of the
 * record, and the two readers of one question cannot answer differently there. `operatorArrangeable`
 * takes no pair and therefore reports the operator's half: it is the answer for "a reader that has
 * only an operator", and a caller that has the node asks `nodeArrangeable`.
 *
 * The record is the only statement of the first three, and every entrance writes into it: a built-in
 * operator's declaration is copied there when the module loads
 * (`operator_table.h#declareSemantics`), and a Camel declaration states it directly.
 * A clause nobody stated reads as design principles 3.3 says it does -- writing the global resource
 * -- rather than as a second answer derived from the table.
 *
 * A GATE is the one node the four terms are not the answer for, and it is answered `not freely
 * arrangeable` whatever its terms are: a gate exists to carry an order, whether it carries one is
 * not one of the four terms, and the reader that decides a placement asks
 * `nodeIsFreelyArrangeable` below, which refuses every gate for that reason. A
 * caller that reads a gate's four terms as "it may be moved" is reading the one node this fact
 * declines to speak for.
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
/// an operator -- the chain predicate of a graph, a listing -- asks this, and gets the operator's
/// answer: for a conversion that is the record's half ("a cast may fault"), because the pair of
/// types the conversion is about is the node's and not the operator's. A reader that has the node
/// asks `nodeArrangeable` below, which reads that pair (file comment).
Arrangeable operatorArrangeable(camel::ops::OperId oper);

/// `core.arrangeable` at any node of `graph`'s view. A branch and a loop stand for what runs inside
/// them: their terms are the conjunction over the nodes of their arms and of their regions. A
/// `Call` and a `Func` node do **not**: what a call runs is another graph's content, and walking the
/// callee's body answers about that body rather than about this node, so the kind declines there
/// (all four terms false, "it does not answer for this node"). That is the answer
/// `nodeIsFreelyArrangeable` gives -- a call is an anchor that may be an effect, and
/// naming the callee directly changes neither its effects nor its faults -- and the two readers of
/// one question answer it the same way.
Arrangeable nodeArrangeable(const graph::GraphView &view, NodeId node, ReadLog &log);

/// The question a placement decision asks: may a pass drop, move, merge or reorder this node, and is
/// it one that needs no order edge after an inline? The answer is `core.arrangeable`'s conjunction
/// at `node`, read out of the store that holds `graph` -- so every pass on either side of the
/// boundary asks one implementation, in the fact layer, and the answer is kept as the fact about
/// this version of the graph rather than recomputed per caller.
///
/// It is deliberately not a second predicate over a kind and an operator: an OPER node's four terms
/// are the operator's record's, a construction's and an access's are what the node does, a loop's
/// termination is its form's, and a branch's are the conjunction over its arms -- a caller holding
/// only a kind and an operator cannot answer three of those, which is why a caller holding a graph
/// asks this and a caller holding only an operator asks `operatorArrangeable`.
///
/// False for a graph no store holds: an answer nobody kept is not one a pass may act on, and a node
/// this kind declines for (a call, a gate) is not freely arrangeable either.
bool nodeIsFreelyArrangeable(const graph::Graph &graph, NodeId node);

/// The four terms as one line: "freely arrangeable", or what fails.
std::string toString(const Arrangeable &arrangeable);

} // namespace camel::facts

