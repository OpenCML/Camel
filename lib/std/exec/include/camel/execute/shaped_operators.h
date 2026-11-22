// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The four operators the compiler still builds as nodes of their own -- CAST, COPY, FILL, ACCS --
 * seen from the execution side as four *shapes*: a conversion, a clone, a construction and an
 * access (refactor stage 2, step 2.3).
 *
 * Why a shape and not a node kind: what a back end needs to know in order to run one of them is
 * "is this a conversion / a copy / a construction / an access, and of what type or position", and
 * the day the front end builds them as ordinary OPER nodes (plan 2.3, steps 1 and 2) that is what
 * the operator record says. A back end that reads `GCNodeKind::Cast` instead ties itself to the
 * representation, and every back end then writes the same four branches. So the four branches are
 * written once, here, by the operator's implementation:
 *
 *   - `shapeOfKind` / `shapeOfNode` is the **one** place in the execution side that asks *which*
 *     of the four a node is. It is the adapter plan 2.3 keeps while the front end still builds the
 *     four nodes; when it stops, this answers from the operator record and nothing below changes.
 *   - `runShapedOperator` is the fast path a frame-running back end takes for one of the four:
 *     the node VM, the macro evaluator and the parallel scheduler's linear path all call it
 *     instead of writing their own arm. What it does with the value -- compute it, write the
 *     node's slot and return it -- is the same in all three, and that is the operator's
 *     implementation. A back end whose failure model differs (the parallel scheduler reports a
 *     failed checked conversion at the node) keeps that policy in its own call site, around this
 *     call.
 *
 * A back end that emits code rather than running a frame (std::fvm, std::cpp) asks `shapeOfNode`
 * for the shape and emits from it; the emitted bytes are that back end's own, because only it
 * knows its instruction set.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"

namespace camel::execute {

/// What one of the four operators does, which is all a back end has to know to run it.
enum class OperShape : uint8_t {
    None = 0,    // not one of the four: the node kind says what it is to the back end
    Conversion,  // CAST: its one ordinary input converted to the node's type
    Clone,       // COPY: a value of its own made from its input
    Construction, // FILL: an aggregate made from a template and its fields
    Access,      // ACCS: a member or an element read out of its input
};

/// The shape a node kind stands for, or None. This is the adapter described at the top of this
/// file: the one place that asks *which* of the four a node is. It is not the last place that
/// names them, and the readings that stay are not this question:
///
///   - **the payload.** A construction's `GCFillBody` (its fill kind, its slots, a closure's form)
///     and an access's `GCAccsBody` (its key and index) are data the operator record does not
///     hold: of the four only `copy` is an operator (`:op/copy`); a conversion, a construction and
///     an access take a compile-time parameter -- a target type, a member position, a fill's kind
///     and field names -- that a record keyed by URI cannot carry yet
///     (`lib/std/builtin/modules/globals.cpp`, refactor plan 2.3). So this answers a shape and
///     hands no payload out, and those reads stay where they are.
///   - **building the node.** The front end still *makes* these four nodes
///     (`kernel/runtime/draft_construct.cpp`): a site that writes one is doing that step, not
///     asking a question the record could answer.
///
/// Both are about what the node holds and how it is made, so they move when the front end stops
/// building the four nodes -- not because this function exists.
inline OperShape shapeOfKind(camel::runtime::GCNodeKind kind) {
    switch (kind) {
    case camel::runtime::GCNodeKind::Cast:
        return OperShape::Conversion;
    case camel::runtime::GCNodeKind::Copy:
        return OperShape::Clone;
    case camel::runtime::GCNodeKind::Fill:
        return OperShape::Construction;
    case camel::runtime::GCNodeKind::Accs:
        return OperShape::Access;
    default:
        return OperShape::None;
    }
}

/// The shape of the operator `ref` stands for, read from the record the plan walks.
inline OperShape shapeOfNode(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = graph.node(ref);
    return node == nullptr ? OperShape::None : shapeOfKind(node->kind);
}

/// The operator's own name, which is what a back end's dump shows for it ("CAST", "COPY", "FILL",
/// "ACCS"). Empty for None.
constexpr const char *shapeName(OperShape shape) {
    switch (shape) {
    case OperShape::Conversion:
        return "CAST";
    case OperShape::Clone:
        return "COPY";
    case OperShape::Construction:
        return "FILL";
    case OperShape::Access:
        return "ACCS";
    case OperShape::None:
        break;
    }
    return "";
}

/// What a back end does with a fault a conversion raises is the back end's own failure model and
/// stays where that model is: `std::prl` catches it at the node, `std::nvm` and `std::macro` let it
/// travel to whoever runs the graph. None of that is the operator's.

/**
 * Runs one of the four on `frame`: the value it computes is written to the node's own data slot and
 * returned. `node` is the plan's record of the node, so its inputs and payload are read without
 * touching the graph again.
 *
 * The three frame-running back ends call this with the same record they walk; the message of a
 * broken invariant names the shape, not the node kind it was built as.
 */
slot_t runShapedOperator(
    const camel::runtime::GCGraph &graph, const camel::runtime::ExecNode &node,
    camel::core::context::Frame &frame);

/// The same for a caller that walks the graph's nodes itself rather than the plan's records (the
/// macro evaluator): the record the operator reads is taken from the graph.
slot_t runShapedOperator(
    const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t node,
    camel::core::context::Frame &frame);

} // namespace camel::execute