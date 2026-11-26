// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The placement questions a pass asks about a node before it drops, moves, merges or reorders it:
 * can running it fault (`nodeMayFault`), is it a statement of a chain
 * (`graph::is_chain_statement`), and the two answers those are built on (`castMayFault`,
 * `shapesAreFixed`). Every pass asks them, on both sides of the boundary -- the C++ passes read the
 * names here, the passes written in Camel read the module `graph`'s builtins, which hand their
 * answer over -- and they ask nothing else.
 *
 * They are declared here rather than in `camel/runtime/graph.h` so that a change to this family
 * recompiles the TUs that ask the questions, not every TU that holds the graph's layout. This
 * header names only a node's kind and id (`node_kind.h`), an operator id (`oper_id.h`) and a type
 * the caller supplies; `GCGraph` is named by reference and is not defined here, which is why a
 * caller of the graph-ref overloads includes `graph.h` as before.
 *
 * The four terms taken together -- may the node be dropped, moved, merged or left without an order
 * edge -- are not stated here: they are the fact layer's `core.arrangeable`
 * (`camel/facts/arrangeable.h`, `nodeIsFreelyArrangeable`), which reads these answers as its terms.
 */

#pragma once

#include "camel/core/oper_id.h"
#include "camel/runtime/node_kind.h"

namespace camel::core::type {
class Type;
}

namespace camel::runtime {

class GCGraph;

/// Whether converting a value of type `source` to `target` when a CAST runs can fail: every
/// conversion the type does not call safe checks the value (an array's shape, a number's range).
bool castMayFault(const camel::core::type::Type *target, const camel::core::type::Type *source);

/// The fault half of the arrangeable question, on its own: whether running a node of this kind can
/// raise a fault (`castMayFault`, `OperatorTraits::mayFault`; design principles 3.9) where it is
/// written. It is the fact that decides whether a statement nobody reads may still be dropped, so
/// the C++ sweep reads it here and `graph::may_fault` (modules/graph) hands the same answer to the
/// passes written in Camel: one definition, so the two cannot disagree. A node whose operator
/// nobody declared is not known never to fault, and neither is a kind this header does not know
/// (a call, a loop, a gate).
///
/// It is **not** the answer to whether a pass may drop or move a node: that is `core.arrangeable`
/// in the fact layer (`camel/facts/arrangeable.h`, `nodeIsFreelyArrangeable`), which states the
/// four terms of design principles 3.4 together. This function is one term of them, kept because
/// the sweep asks it about a node it is not otherwise reading.
bool nodeMayFault(
    GCNodeKind kind, camel::core::OperId oper, const camel::core::type::Type *castTarget = nullptr,
    const camel::core::type::Type *castSource = nullptr, bool fixedShapes = false);

/// Whether a type states its shape in full: not an array, or an array of fixed rank whose
/// extents are all constants.
bool shapesAreFixed(const camel::core::type::Type *type);

/// Whether a node of this kind is a statement of a chain: an operator that has an effect or may
/// fault, a cast whose conversion may fail, a CALL, a BRCH or LOOP that owns a control region, or a
/// GATE. A JOIN is not an independent statement. A direct FUNC call has the same possible effects
/// and faults as a CALL. A pure, infallible operator or cast is not one either;
/// `core.arrangeable` answers the separate question of whether a pass may move it.
/// `standsForRegion` is `nodeStandsForItsRegion` where the caller has a graph.
namespace graph {
bool is_chain_statement(
    GCNodeKind kind, camel::core::OperId oper, const camel::core::type::Type *castTarget = nullptr,
    const camel::core::type::Type *castSource = nullptr, bool standsForRegion = false);
} // namespace graph

inline bool isChainStatement(
    GCNodeKind kind, camel::core::OperId oper, const camel::core::type::Type *castTarget = nullptr,
    const camel::core::type::Type *castSource = nullptr, bool standsForRegion = false) {
    return graph::is_chain_statement(kind, oper, castTarget, castSource, standsForRegion);
}

/// `nodeMayFault` of a node of a graph, read the way the other graph questions read their operands
/// and types: an OPER node's operator from its payload, a cast's two types from its input.
bool nodeMayFault(const GCGraph &graph, gc_node_ref_t ref);

/// `graph::is_chain_statement` of a node of a graph: the statement question asked with the graph
/// that says whether the node stands for its region. `graph.h` keeps the chain's shape (its exit,
/// and whether it can hand the chain on).
namespace graph {
bool is_chain_statement(const GCGraph &graph, gc_node_ref_t ref);
} // namespace graph

inline bool isChainStatement(const GCGraph &graph, gc_node_ref_t ref) {
    return graph::is_chain_statement(graph, ref);
}

} // namespace camel::runtime