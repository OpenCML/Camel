// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The four shaped operators' fast paths (see camel/execute/shaped_operators.h). One function per
 * shape, written next to the operator it runs and shared by every back end that runs a frame, so
 * that no back end writes a branch of its own for CAST, COPY, FILL or ACCS.
 *
 * The bodies are the ones the node VM, the macro evaluator and the parallel scheduler each carried
 * a copy of. A caller that reports a failed conversion differently (the parallel scheduler reports
 * it at the node) keeps that in its own call site: the failure model belongs to the back end.
 */

#include "camel/execute/shaped_operators.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/utils/assert.h"

namespace camel::execute {

namespace {

using camel::core::context::Frame;
using camel::core::rtdata::Object;
using camel::runtime::gc_data_idx_t;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCAccsKind;
using camel::runtime::GCFillBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;

/// The data slot of `ref` in the frame of its graph. Every plan caches this per node
/// (`ExecPlan::dataIndexByRef`), and the cache is filled with exactly this reading, so a node's own
/// record is the one place the answer comes from.
gc_data_idx_t slotOf(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    GUARD(node != nullptr, "a shaped operator's input resolved to no node.");
    return node->dataIndex;
}

/// CAST: the value converted to the type the node says it produces. A conversion that fails raises
/// the fault the conversion itself raises; where that fault is reported is the calling back end's
/// policy, so nothing is caught here.
slot_t runConversion(
    const GCGraph &graph, const camel::runtime::ExecNode &node, Frame &frame) {
    const auto inputs = node.normInputs();
    GUARD(!inputs.empty(), "a conversion must have one ordinary input.");
    const gc_data_idx_t srcIndex = slotOf(graph, inputs.front());
    camel::core::type::Type *srcType = frame.typeAt<camel::core::type::Type>(srcIndex);
    camel::core::type::Type *tgtType = node.node->dataType();
    const slot_t value               = frame.get<slot_t>(srcIndex);
    const slot_t result              = tgtType->castSlotFrom(value, srcType);
    frame.set(node.node->dataIndex, result);
    return result;
}

/// COPY: a value of its own from the input -- a clone when the value is traced, the value itself
/// when it is not (values have no identity, design principles 10.5).
slot_t runClone(const GCGraph &graph, const camel::runtime::ExecNode &node, Frame &frame) {
    const auto inputs = node.normInputs();
    GUARD(!inputs.empty(), "a copy must have one ordinary input.");
    const gc_data_idx_t srcIndex = slotOf(graph, inputs.front());
    const camel::core::type::TypeCode srcCode = frame.codeAt(srcIndex);
    if (camel::core::type::isGCTraced(srcCode)) {
        Object *source   = frame.get<Object *>(srcIndex);
        auto *srcTypePtr = frame.typeAt<camel::core::type::Type>(srcIndex);
        Object *copy     = source->clone(camel::core::mm::autoSpace(), srcTypePtr, false);
        frame.set(node.node->dataIndex, copy);
        return camel::core::rtdata::toSlot(copy);
    }
    const slot_t value = frame.get<slot_t>(srcIndex);
    frame.set(node.node->dataIndex, value);
    return value;
}

/// FILL: an aggregate made from the template its first ordinary input carries and the values its
/// `with` inputs are.
slot_t runConstruction(
    const GCGraph &graph, const camel::runtime::ExecNode &node, Frame &frame) {
    const auto inputs     = node.normInputs();
    const auto fieldInputs = node.withInputs();
    GUARD(!inputs.empty(), "a construction must have one ordinary input.");
    const gc_data_idx_t srcIndex = slotOf(graph, inputs.front());
    camel::core::type::Type *type = node.node->dataType();
    GUARD(
        camel::core::type::isGCTraced(type->code()),
        "a construction's target type is not GC-traced.");
    Object *templateValue = frame.get<Object *>(srcIndex);
    GUARD(templateValue != nullptr, "a construction's template value is null.");
    Object *made = templateValue->clone(camel::core::mm::autoSpace(), type, false);
    GUARD(made != nullptr, "a construction's target value is null.");
    std::vector<slot_t> values;
    values.reserve(fieldInputs.size());
    for (const gc_node_ref_t input : fieldInputs) {
        values.push_back(frame.get<slot_t>(slotOf(graph, input)));
    }
    writeRuntimeFillSlots(made, type, node.bodyAs<GCFillBody>(), values);
    frame.set(node.node->dataIndex, made);
    return camel::core::rtdata::toSlot(made);
}

/// ACCS: one member of a tuple (by index) or of a struct (by key) read out of the input.
slot_t runAccess(const GCGraph &graph, const camel::runtime::ExecNode &node, Frame &frame) {
    const auto inputs = node.normInputs();
    GUARD(!inputs.empty(), "an access must have one ordinary input.");
    const gc_data_idx_t srcIndex = slotOf(graph, inputs.front());
    const auto *body             = node.bodyAs<GCAccsBody>();
    GUARD(body != nullptr, "an access must carry its position.");
    if (body->accsKind == GCAccsKind::TupleIndex) {
        auto *tuple = frame.get<Tuple *>(srcIndex);
        GUARD(tuple != nullptr, "an access's tuple value is null.");
        GUARD(body->value < tuple->size(), "an access's tuple index is out of bounds.");
        const slot_t value = tuple->get<slot_t>(body->value);
        frame.set(node.node->dataIndex, value);
        return value;
    }
    const std::string_view keyView = body->key();
    const std::string key(keyView.data(), keyView.size());
    auto *object = frame.get<Struct *>(srcIndex);
    GUARD(object != nullptr, "an access's struct value is null.");
    auto *structType = frame.typeAt<camel::core::type::Type>(srcIndex);
    const slot_t value = object->get<slot_t>(key, structType);
    frame.set(node.node->dataIndex, value);
    return value;
}

} // namespace

slot_t runShapedOperator(
    const GCGraph &graph, const camel::runtime::ExecNode &node, Frame &frame) {
    switch (shapeOfKind(node.node->kind)) {
    case OperShape::Conversion:
        return runConversion(graph, node, frame);
    case OperShape::Clone:
        return runClone(graph, node, frame);
    case OperShape::Construction:
        return runConstruction(graph, node, frame);
    case OperShape::Access:
        return runAccess(graph, node, frame);
    // No kind stands for an entering or a refinement: both are OPER nodes with their own kernel
    // (`operators/cast.cpp`), and this function runs a *kind's* frame. The labels name the shapes
    // the switch would otherwise leave out, and they reach the answer the kind's absence deserves.
    case OperShape::Entering:
    case OperShape::Refining:
    case OperShape::None:
        break;
    }
    GUARD(false, "a shaped operator was run on a node that is not one of the four.");
    return NullSlot;
}

slot_t runShapedOperator(const GCGraph &graph, gc_node_ref_t ref, Frame &frame) {
    const GCNode *record = graph.node(ref);
    GUARD(record != nullptr, "a shaped operator's node resolved to nothing.");
    const auto norm = graph.normInputsOf(ref);
    const auto with = graph.withInputsOf(ref);
    camel::runtime::ExecNode node;
    node.ref       = ref;
    node.node      = record;
    node.norm      = norm.data();
    node.normCount = static_cast<uint32_t>(norm.size());
    node.with      = with.data();
    node.withCount = static_cast<uint32_t>(with.size());
    node.body      = record->hasPayload() ? graph.nodePayloadBytes(ref).data() : nullptr;
    return runShapedOperator(graph, node, frame);
}

} // namespace camel::execute