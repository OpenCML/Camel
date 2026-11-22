// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A conversion, a copy, a construction and a member read, seen from the execution side as four
 * *shapes* (refactor stage 2, step 2.3). `cast`, `copy`, `fill` and `access` are declared operators
 * (`src/stdlib/prelude.cml`), and the front end still builds the same four operations as nodes of
 * their own kinds -- CAST, COPY, FILL, ACCS -- so each question about one of them has two forms to
 * answer for.
 *
 * Why a shape and not a node kind: what a back end needs to know is "is this a conversion / a copy
 * / a construction / an access, and of what type or position". When the front end builds the four
 * as operator calls, that is what the operator record says; a reader that tests `GCNodeKind::Cast`
 * instead ties itself to the representation, and every back end would then grow the same four
 * branches. So the question is asked here, in two parts, because there are two questions:
 *
 *   - `shapeOfOperation` answers *which of the four operations a node performs*, for both forms: it
 *     asks the **operator** an OPER node names (`:op/cast`, `:op/copy`, `:op/fill`, `:op/access`,
 *     `:op/access_key`) and reads the kind for a node of one of the four kinds, which is what the
 *     front end still builds. A classification, an analysis and a plan ask this.
 *   - `shapeOfKind` / `shapeOfNode` answer the other question: which kind's body a node carries.
 *     That is what a reader of the payload is asking, since those bytes are the kinds' payloads
 *     (a construction's `GCFillBody`, an access's `GCAccsBody`). An OPER node answers None there,
 *     and that is not a gap: its payload is the operator's record. Asking `shapeOfOperation` and
 *     then reading a fill body is how a `:op/fill` call comes to be read as a construction: its
 *     slot count is then taken from an operator id and its `slots()` points anywhere.
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

#include <string_view>

namespace camel::execute {

/// What one of the four operators does, which is all a back end has to know to run it.
enum class OperShape : uint8_t {
    None = 0,     // not one of the four: the node kind says what it is to the back end
    Conversion,   // CAST: its one ordinary input converted to the node's type
    Clone,        // COPY: a value of its own made from its input
    Construction, // FILL: an aggregate made from a template and its fields
    Access,       // ACCS: a member or an element read out of its input
};

/// The shape a node **kind** stands for, or None: the kind half of the question, and the answer for
/// the four kinds the front end still builds. This is what a reader asks when it is about to read
/// the kind's own body -- a construction's slots, an access's position, a conversion's target type
/// -- because those bytes belong to the kind (the file comment says what goes wrong when the other
/// question is asked instead).
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

/// The shape the operator `uri` names, or None for an operator that is not one of the four. The two
/// member reads are two operators -- by position and by field name -- and both are an `Access`
/// here, because what a back end or an analysis does with either is read a member out of the value
/// it was given.
inline OperShape shapeOfUri(std::string_view uri) {
    if (uri == ":op/cast") {
        return OperShape::Conversion;
    }
    if (uri == ":op/copy") {
        return OperShape::Clone;
    }
    if (uri == ":op/fill") {
        return OperShape::Construction;
    }
    if (uri == ":op/access" || uri == ":op/access_key") {
        return OperShape::Access;
    }
    return OperShape::None;
}

/// The shape of the **operation** `ref` performs: the operator an OPER node names, and the kind for
/// a node of one of the four kinds. The two forms answer one question, and this is the one to ask
/// when what a node does decides what the reader does with it -- a classification, an analysis, a
/// plan, an escape or alias verdict.
///
/// It is **not** the question to ask before reading one of the four bodies. A construction's
/// `GCFillBody` and an access's `GCAccsBody` are the kinds' payload, and an OPER node's payload is
/// its record, so a reader that gets `Construction` here for a `:op/fill` call and then reads the
/// fill body reads a record as a slot list. A reader about to read a body asks `shapeOfKind` /
/// `shapeOfNode`, which answer None for an OPER node for exactly this reason, and a reader that
/// needs a construction's fields out of a call reads the call's operands.
inline OperShape
shapeOfOperation(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = graph.node(ref);
    if (node == nullptr) {
        return OperShape::None;
    }
    if (node->kind != camel::runtime::GCNodeKind::Oper) {
        return shapeOfKind(node->kind);
    }
    if (!node->hasPayload()) {
        return OperShape::None;
    }
    const auto *body = graph.nodeBodyAs<camel::runtime::GCOperBody>(ref);
    return body == nullptr ? OperShape::None : shapeOfUri(body->uri());
}

/// The shape a node **of this graph** stands for **by its kind**, or None. A reader that runs or
/// emits one of the four bodies asks this: an OPER node answers None here even when its operator is
/// one of the four, because its body is the operator's record and not a construction's slots or an
/// access's position. What operation a node performs is `shapeOfOperation`.
inline OperShape
shapeOfNode(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
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
 * It is for a node of one of the four **kinds**: the four bodies read the construction's slots and
 * the access's position out of the kind's payload, which an OPER node does not hold. An OPER node
 * that names one of the four is run by that operator's own implementation, on the ordinary operator
 * path, and a back end that takes this fast path on its kind alone leaves it there.
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