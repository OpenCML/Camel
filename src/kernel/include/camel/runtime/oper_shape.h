// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which of the four operations a node performs, read from the operator's record.
 *
 * These are the kernel's answers. The shape is a property the operator is registered with
 * (`camel/ops/oper_shape.h`, `Record::shape`), so a kernel reader asks the record and never the
 * letters of a URI -- which is the one thing the operator table says a reader must not do (design
 * section 3.8, R10) -- and no table of names exists to drift from the registration that states it.
 *
 * The two questions, and why they are two:
 *
 *   - `shapeOfOperation` answers *which of the four operations a node performs*, for both forms: it
 *     reads the operator record an OPER node names, and the kind for a node of one of the four
 *     kinds, which is what the front end still builds. A classification, an analysis and a plan ask
 *     this.
 *   - `shapeOfKind` / `shapeOfNode` answer the other question: which kind's body a node carries.
 *     That is what a reader of the payload is asking, since those bytes are the kinds' payloads (a
 *     construction's `GCFillBody`, an access's `GCAccsBody`). An OPER node answers None there, and
 *     that is not a gap: its payload is the operator's record. Asking `shapeOfOperation` and then
 *     reading a fill body is how a `:op/fill` call comes to be read as a construction: its slot
 *     count is then taken from an operator id and its `slots()` points anywhere.
 *
 * What a back end *does* with one of the four -- the fast paths that run them and the emitted code
 * -- stays in the library (`camel/execute/shaped_operators.h`), where it has the implementations of
 * the four operators. The kernel only names the shape.
 */

#pragma once

#include "camel/core/operator_table.h"
#include "camel/ops/oper_shape.h"
#include "camel/ops/operator_record.h"
#include "camel/runtime/graph.h"

namespace camel::runtime {

using camel::ops::OperShape;

/// The shape the operator `id` states in its record, or None -- an operator that is not one of the
/// four, and one nobody declared anything about. Lock-free: the record is published once, at load
/// time and the id never moves; the table joins the id a node holds to the record's own id at
/// intern (`OperatorTable::recordOf`), so this asks nothing by URI text.
inline OperShape shapeOfOperator(camel::core::OperId id) noexcept {
    const camel::core::OperatorTable &table = camel::core::OperatorTable::instance();
    if (id == camel::core::kNoOper || id >= table.size()) {
        return OperShape::None;
    }
    const camel::ops::Record *record = table.recordOf(id);
    return record == nullptr ? OperShape::None : record->shape;
}

/// The shape a node **kind** stands for, or None: the kind half of the question, and the answer for
/// the four kinds the front end still builds. This is what a reader asks when it is about to read
/// the kind's own body -- a construction's slots, an access's position, a conversion's target type
/// -- because those bytes belong to the kind (the file comment says what goes wrong when the other
/// question is asked instead).
inline OperShape shapeOfKind(camel::runtime::GCNodeKind kind) noexcept {
    switch (kind) {
    case camel::runtime::GCNodeKind::Fill:
        return OperShape::Construction;
    case camel::runtime::GCNodeKind::Accs:
        return OperShape::Access;
    default:
        return OperShape::None;
    }
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
inline OperShape shapeOfOperation(const GCGraph &graph, gc_node_ref_t ref) noexcept {
    const GCNode *node = graph.node(ref);
    if (node == nullptr) {
        return OperShape::None;
    }
    if (node->kind != GCNodeKind::Oper) {
        return shapeOfKind(node->kind);
    }
    if (!node->hasPayload()) {
        return OperShape::None;
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
    return body == nullptr ? OperShape::None : shapeOfOperator(body->oper);
}

/// The shape a node **of this graph** stands for **by its kind**, or None. A reader that runs or
/// emits one of the four bodies asks this: an OPER node answers None here even when its operator is
/// one of the four, because its body is the operator's record and not a construction's slots or an
/// access's position. What operation a node performs is `shapeOfOperation`.
inline OperShape shapeOfNode(const GCGraph &graph, gc_node_ref_t ref) noexcept {
    const GCNode *node = graph.node(ref);
    return node == nullptr ? OperShape::None : shapeOfKind(node->kind);
}

} // namespace camel::runtime