// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The four shaped operators' fast paths, and the library's names for the question that says which
 * of the four a node performs.
 *
 * **Where the question lives.** It is the kernel's (`camel/runtime/oper_shape.h`): the shape is
 * a property of the operator's record, so a kernel reader does not include a standard-library
 * header to ask it. This header re-exports those names, so the back ends, the analyses and the
 * passes that already ask `camel::execute::shapeOfOperation` keep asking it, of one implementation.
 *
 * The two questions, and why they are two:
 *
 *   - `shapeOfOperation` answers *which of the four operations a node performs*, for both forms: it
 *     asks the operator record an OPER node names (`:op/cast`, `:op/copy`, `:op/fill`,
 * `:op/access`,
 *     `:op/access_key`, whose shapes are stated where those operators are registered), and reads
 * the kind for a node of one of the four kinds, which is what the front end still builds. A
 *     classification, an analysis and a plan ask this.
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
 *
 * **No name table.** Every reader asks the record (`camel/runtime/oper_shape.h`), by the operator a
 * node names: the shapes are stated where the four are registered, so there is nothing here that
 * could drift from that statement. A table of URI text was the last reader of the kind, and the
 * exporter and the shape test are the two callers that used to read it -- both ask the record now.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/ops/oper_shape.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/oper_shape.h"

#include <string_view>

namespace camel::execute {

using camel::ops::OperShape;
using camel::ops::shapeName;
using camel::runtime::shapeOfKind;
using camel::runtime::shapeOfNode;
using camel::runtime::shapeOfOperation;

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
