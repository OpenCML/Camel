/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * LoopView, LoopDraftView, the effect summary of a graph, and the commit-time check of a LOOP
 * node against its two graphs (camel/runtime/loop.h).
 */

#include "camel/runtime/loop.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/utils/assert.h"

#include <format>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace camel::runtime {

using namespace camel::core::type;
using camel::core::loopHasCondition;
using camel::core::loopHasIterable;
using camel::core::rtdata::fromSlot;

namespace {

/// The capture ports of a loop graph: its with-ports once it is called (the builder makes the
/// loop graphs so), its closure ports otherwise.
std::span<const gc_node_ref_t> capturePortsOf(const GCGraph *graph) {
    return graph->withPorts().empty() ? graph->closureNodes() : graph->withPorts();
}

[[noreturn]] void loopFails(const GCGraph *graph, gc_node_ref_t node, const std::string &why) {
    throw std::runtime_error(
        std::format("Runtime graph '{}' has LOOP node {}: {}.", graph->name(), node, why));
}

std::string typeText(const Type *type) { return type ? type->toString() : std::string("none"); }

/// The node a tuple FILL writes into position `slot`, or nullopt when `result` is not a tuple
/// FILL or no input writes that position (the template's constant stays).
std::optional<gc_node_ref_t>
fillComponent(const GCGraph *graph, gc_node_ref_t result, size_t slot) {
    const GCNode *node = graph->node(result);
    if (node == nullptr || node->kind != GCNodeKind::Fill) {
        return std::nullopt;
    }
    const auto *fill = graph->nodeBodyAs<GCFillBody>(result);
    if (fill->fillKind != GCFillKind::Tuple) {
        return std::nullopt;
    }
    // A FILL copies its template (Norm input 0) and writes With input i into position
    // slots()[i]; a position no input writes keeps the template's constant.
    const auto slots  = fill->slots();
    const auto inputs = graph->withInputsOf(result);
    for (size_t i = 0; i < slots.size() && i < inputs.size(); ++i) {
        if (static_cast<size_t>(slots[i]) == slot) {
            return inputs[i];
        }
    }
    return std::nullopt;
}

} // namespace

LoopDraftView::LoopDraftView(const Program &program, const GraphDraft &draft, gc_node_ref_t node)
    : draft_(&draft), node_(node) {
    const DraftNodeHeader *header = draft.header(node);
    ASSERT(
        header != nullptr && header->kind == GCNodeKind::Loop,
        "LoopDraftView of a node that is not a LOOP node.");
    const auto *body  = reinterpret_cast<const GCLoopBody *>(draft.payloadOf(node).data());
    kind_             = body->kind;
    cond_             = program.graph(body->condGraph);
    body_             = program.graph(body->bodyGraph);
    stateCount_       = draft.normInputsOf(node).size() - (loopHasIterable(kind_) ? 1 : 0);
    collectionCount_  = body->collectionCount;
}

Type *LoopDraftView::valueType() const { return draft_->header(node_)->dataType; }

Type *LoopDraftView::stateType(size_t j) const { return body_->node(bodyStateParam(j))->dataType(); }

Type *LoopDraftView::collectionArrayType(size_t c) const {
    ASSERT(c < collectionCount_, "LoopDraftView: the loop has no such collection slot.");
    const auto *value = static_cast<const TupleType *>(valueType());
    ASSERT(
        value != nullptr && value->size() == stateCount_ + collectionCount_,
        "LoopDraftView: the loop's value type does not have the k + s components its shape says.");
    return value->typeAt(collectionValueIndex(c));
}

Type *LoopDraftView::collectionElemType(size_t c) const {
    const Type *array = collectionArrayType(c);
    ASSERT(
        array != nullptr && array->code() == TypeCode::Array,
        "LoopDraftView: a collection slot's value type is not an array.");
    return static_cast<const ArrayType *>(array)->elemType();
}

std::optional<gc_node_ref_t> LoopDraftView::iterable() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return draft_->normInputsOf(node_).front();
}

gc_node_ref_t LoopDraftView::initialState(size_t j) const {
    ASSERT(j < stateCount_, "LoopDraftView: no such value of the state.");
    return draft_->normInputsOf(node_)[j + (loopHasIterable(kind_) ? 1 : 0)];
}

std::span<const gc_node_ref_t> LoopDraftView::captures() const {
    return draft_->withInputsOf(node_);
}

gc_node_ref_t LoopDraftView::condStateParam(size_t j) const {
    ASSERT(cond_ != nullptr && j < stateCount_, "LoopDraftView: no such condition parameter.");
    return cond_->normPorts()[j];
}

std::optional<gc_node_ref_t> LoopDraftView::bodyElementParam() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return body_->normPorts().front();
}

gc_node_ref_t LoopDraftView::bodyStateParam(size_t j) const {
    ASSERT(j < stateCount_, "LoopDraftView: no such body parameter.");
    return body_->normPorts()[j + (loopHasIterable(kind_) ? 1 : 0)];
}

gc_node_ref_t LoopDraftView::condCapturePort(size_t c) const {
    ASSERT(cond_ != nullptr, "LoopDraftView: a For loop has no condition graph.");
    return capturePortsOf(cond_)[c];
}

gc_node_ref_t LoopDraftView::bodyCapturePort(size_t c) const {
    return capturePortsOf(body_)[c];
}

gc_node_ref_t LoopDraftView::nextState() const { return body_->returnNodeRef(); }

Type *LoopDraftView::bodyResultType() const { return body_->funcType()->exitType(); }

LoopView::LoopView(const Program &program, const GCGraph &graph, gc_node_ref_t node)
    : program_(&program), graph_(&graph), node_(node) {
    const GCNode *record = graph.node(node);
    ASSERT(
        record != nullptr && record->kind == GCNodeKind::Loop,
        "LoopView of a node that is not a LOOP node.");
    kind_            = graph.loopKindOf(node);
    cond_            = program.loopCondOf(graph, node);
    body_            = program.loopBodyOf(graph, node);
    stateCount_      = graph.normInputsOf(node).size() - (loopHasIterable(kind_) ? 1 : 0);
    collectionCount_ = graph.loopCollectionCountOf(node);
}

Type *LoopView::stateType(size_t j) const { return body_->node(bodyStateParam(j))->dataType(); }

Type *LoopView::collectionArrayType(size_t c) const {
    ASSERT(c < collectionCount_, "LoopView: the loop has no such collection slot.");
    const auto *value = static_cast<const TupleType *>(valueType());
    ASSERT(
        value != nullptr && value->size() == stateCount_ + collectionCount_,
        "LoopView: the loop's value type does not have the k + s components its shape says.");
    return value->typeAt(collectionValueIndex(c));
}

Type *LoopView::collectionElemType(size_t c) const {
    const Type *array = collectionArrayType(c);
    ASSERT(
        array != nullptr && array->code() == TypeCode::Array,
        "LoopView: a collection slot's value type is not an array.");
    return static_cast<const ArrayType *>(array)->elemType();
}

std::optional<gc_node_ref_t> LoopView::iterable() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return graph_->normInputsOf(node_).front();
}

gc_node_ref_t LoopView::initialState(size_t j) const {
    ASSERT(j < stateCount_, "LoopView: no such value of the state.");
    return graph_->normInputsOf(node_)[j + (loopHasIterable(kind_) ? 1 : 0)];
}

gc_node_ref_t LoopView::condStateParam(size_t j) const {
    ASSERT(cond_ != nullptr && j < stateCount_, "LoopView: no such condition parameter.");
    return cond_->normPorts()[j];
}

std::optional<gc_node_ref_t> LoopView::bodyElementParam() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return body_->normPorts().front();
}

gc_node_ref_t LoopView::bodyStateParam(size_t j) const {
    ASSERT(j < stateCount_, "LoopView: no such body parameter.");
    return body_->normPorts()[j + (loopHasIterable(kind_) ? 1 : 0)];
}

gc_node_ref_t LoopView::condCapturePort(size_t c) const {
    ASSERT(cond_ != nullptr, "LoopView: a For loop has no condition graph.");
    return capturePortsOf(cond_)[c];
}

gc_node_ref_t LoopView::bodyCapturePort(size_t c) const { return capturePortsOf(body_)[c]; }

gc_node_ref_t LoopView::nextState() const { return body_->returnNodeRef(); }

std::optional<gc_node_ref_t> LoopView::nextStateComponent(size_t j) const {
    if (!nextStateIsTuple()) {
        return std::nullopt;
    }
    return fillComponent(body_, nextState(), j);
}

std::optional<gc_node_ref_t> LoopView::nextCollectionComponent(size_t c) const {
    if (collectionCount_ == 0 || c >= collectionCount_) {
        return std::nullopt;
    }
    return fillComponent(body_, nextState(), collectionValueIndex(c));
}

EffectSummary LoopView::effects() const {
    EffectSummary summary = effectSummaryOf(*program_, cond_);
    summary |= effectSummaryOf(*program_, body_);
    summary.mayFault        = true;
    summary.mayNotTerminate = true;
    return summary;
}

slot_t loopValueComponent(const LoopView &loop, slot_t value, size_t index) {
    if (!loop.valueIsTuple()) {
        ASSERT(index == 0, "A loop that does not wrap its state has no other component.");
        return value;
    }
    ASSERT(
        index < loop.valueComponentCount(),
        "A component outside the loop's value tuple was asked for.");
    return fromSlot<Tuple *>(value)->get<slot_t>(index);
}

slot_t loopStateValue(const LoopView &loop, slot_t value, size_t j) {
    ASSERT(j < loop.stateCount(), "The loop has no such state value.");
    return loopValueComponent(loop, value, loop.stateValueIndex(j));
}

slot_t loopCollectedValue(const LoopView &loop, slot_t value, size_t c) {
    ASSERT(c < loop.collectionCount(), "The loop has no such collection slot.");
    return loopValueComponent(loop, value, loop.collectionValueIndex(c));
}

void validateRuntimeLoopInterface(
    const Program &program, const GCGraph *graph, gc_node_ref_t node) {
    const LoopKind kind = graph->loopKindOf(node);
    const GCGraph *cond = program.loopCondOf(*graph, node);
    const GCGraph *body = program.loopBodyOf(*graph, node);
    if (body == nullptr || (loopHasCondition(kind) != (cond != nullptr))) {
        loopFails(graph, node, "its graphs do not match its form");
    }
    if (!body->hasNodePayload() || (cond && !cond->hasNodePayload())) {
        loopFails(graph, node, "a graph of the loop is not encoded");
    }
    const auto norm         = graph->normInputsOf(node);
    const size_t iterableAt = loopHasIterable(kind) ? 1 : 0;
    if (norm.size() <= iterableAt) {
        loopFails(graph, node, "it has no state");
    }
    const size_t k = norm.size() - iterableAt;
    const size_t m = graph->withInputsOf(node).size();

    // I19: the ports. Both graphs take the k values of the state (the body the element
    // first) and the m captures, in the order of the node's With inputs.
    if (body->normPorts().size() != iterableAt + k || capturePortsOf(body).size() != m) {
        loopFails(
            graph,
            node,
            std::format(
                "its body graph '{}' takes {} values and {} captures, the node passes {} and {}",
                body->name(),
                body->normPorts().size(),
                capturePortsOf(body).size(),
                iterableAt + k,
                m));
    }
    if (cond && (cond->normPorts().size() != k || capturePortsOf(cond).size() != m)) {
        loopFails(
            graph,
            node,
            std::format(
                "its condition graph '{}' takes {} values and {} captures, the node passes {} "
                "and {}",
                cond->name(),
                cond->normPorts().size(),
                capturePortsOf(cond).size(),
                k,
                m));
    }
    for (size_t c = 0; cond && c < m; ++c) {
        Type *a = cond->node(capturePortsOf(cond)[c])->dataType();
        Type *b = body->node(capturePortsOf(body)[c])->dataType();
        if (a != b && !(a && b && a->equals(b))) {
            loopFails(
                graph,
                node,
                std::format(
                    "capture {} is a '{}' in the condition graph and a '{}' in the body graph",
                    c,
                    typeText(a),
                    typeText(b)));
        }
    }

    // I18: the types. The state types are those of the body's state ports; the condition's
    // ports agree with them; each first value can be assigned to its state; the body returns
    // the state; the condition returns bool; the element port takes the array's elements.
    std::vector<Type *> stateTypes;
    for (size_t j = 0; j < k; ++j) {
        stateTypes.push_back(body->node(body->normPorts()[iterableAt + j])->dataType());
        Type *initial = graph->node(norm[iterableAt + j])->dataType();
        if (stateTypes[j] && initial && !stateTypes[j]->assignableFrom(initial)) {
            loopFails(
                graph,
                node,
                std::format(
                    "the first value of state {} is a '{}', the state is a '{}'",
                    j,
                    typeText(initial),
                    typeText(stateTypes[j])));
        }
        if (cond) {
            Type *condType = cond->node(cond->normPorts()[j])->dataType();
            if (condType != stateTypes[j] &&
                !(condType && stateTypes[j] && condType->equals(stateTypes[j]))) {
                loopFails(
                    graph,
                    node,
                    std::format(
                        "state {} is a '{}' in the condition graph and a '{}' in the body graph",
                        j,
                        typeText(condType),
                        typeText(stateTypes[j])));
            }
        }
    }
    Type *valueType     = graph->node(node)->dataType();
    const size_t s      = graph->loopCollectionCountOf(node);
    Type *expected      = nullptr;
    Type *expectedValue = nullptr;
    std::vector<Type *> collectedTypes;
    if (s == 0) {
        expected = k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));
        expectedValue = expected;
    } else {
        // A collecting loop's state and its tapes travel together in one tuple: the k state
        // values first, so a consumer that ignores the tapes reads the state where it used to.
        if (valueType == nullptr || valueType->code() != TypeCode::Tuple) {
            loopFails(
                graph,
                node,
                std::format(
                    "it collects {} values but its value is a '{}', not a tuple",
                    s,
                    typeText(valueType)));
        }
        const auto *valueTuple = static_cast<const TupleType *>(valueType);
        if (valueTuple->size() != k + s) {
            loopFails(
                graph,
                node,
                std::format(
                    "its value tuple has {} components, its {} state values and {} collection "
                    "slots need {}",
                    valueTuple->size(),
                    k,
                    s,
                    k + s));
        }
        for (size_t j = 0; j < k; ++j) {
            Type *component = valueTuple->typeAt(j);
            if (component != stateTypes[j] &&
                !(component && stateTypes[j] && component->equals(stateTypes[j]))) {
                loopFails(
                    graph,
                    node,
                    std::format(
                        "component {} of its value is a '{}', the state is a '{}'",
                        j,
                        typeText(component),
                        typeText(stateTypes[j])));
            }
        }
        for (size_t c = 0; c < s; ++c) {
            Type *array = valueTuple->typeAt(k + c);
            if (array == nullptr || array->code() != TypeCode::Array) {
                loopFails(
                    graph,
                    node,
                    std::format(
                        "component {} of its value is a '{}', a collection slot holds an array",
                        k + c,
                        typeText(array)));
            }
            collectedTypes.push_back(static_cast<ArrayType *>(array)->elemType());
            if (collectedTypes.back() == nullptr) {
                loopFails(graph, node, std::format("collection slot {} has no element type", c));
            }
        }
        std::vector<Type *> bodyTypes = stateTypes;
        bodyTypes.insert(bodyTypes.end(), collectedTypes.begin(), collectedTypes.end());
        expected                       = TupleType::create(std::move(bodyTypes));
        std::vector<Type *> valueTypes = stateTypes;
        for (Type *collected : collectedTypes) {
            valueTypes.push_back(ArrayType::create(collected));
        }
        expectedValue = TupleType::create(std::move(valueTypes));
    }
    Type *bodyExit = body->funcType() ? body->funcType()->exitType() : nullptr;
    if (bodyExit == nullptr ||
        (s == 0 ? !expected->assignableFrom(bodyExit) : !expected->equals(bodyExit))) {
        loopFails(
            graph,
            node,
            std::format(
                "its body graph returns a '{}', the {} is '{}'",
                typeText(bodyExit),
                s == 0 ? "state" : "next state and collected values",
                typeText(expected)));
    }
    if (valueType == nullptr || !valueType->equals(expectedValue)) {
        loopFails(
            graph,
            node,
            std::format(
                "its value is a '{}', the {} is '{}'",
                typeText(valueType),
                s == 0 ? "state" : "state and collected arrays",
                typeText(expectedValue)));
    }
    if (cond) {
        Type *condExit = cond->funcType() ? cond->funcType()->exitType() : nullptr;
        if (condExit == nullptr || condExit->code() != TypeCode::Bool) {
            loopFails(
                graph,
                node,
                std::format("its condition graph returns a '{}', not a bool", typeText(condExit)));
        }
    }
    if (iterableAt == 1) {
        Type *sequence = graph->node(norm.front())->dataType();
        if (sequence == nullptr || sequence->code() != TypeCode::Array) {
            loopFails(graph, node, std::format("it iterates over a '{}'", typeText(sequence)));
        }
        Type *element = static_cast<ArrayType *>(sequence)->elemType();
        Type *port    = body->node(body->normPorts().front())->dataType();
        if (port == nullptr || element == nullptr || !port->assignableFrom(element)) {
            loopFails(
                graph,
                node,
                std::format(
                    "its body takes elements of type '{}' from an array of '{}'",
                    typeText(port),
                    typeText(element)));
        }
    }
}

size_t fallBackToCopyingAppend(GraphDraft &body) {
    size_t retargeted = 0;
    for (size_t id = 0; id < body.nodeSlotCount(); ++id) {
        const auto ref     = static_cast<gc_node_ref_t>(id);
        const DraftNode *n = body.node(ref);
        if (n == nullptr || n->header.kind != GCNodeKind::Oper) {
            continue;
        }
        const auto payload = body.payloadOf(n);
        if (payload.size() < sizeof(GCOperBody)) {
            continue;
        }
        if (reinterpret_cast<const GCOperBody *>(payload.data())->uri() != ":struct/append_arr_owned") {
            continue;
        }
        body.retargetOperNode(ref, ":struct/append_arr");
        ++retargeted;
    }
    return retargeted;
}

} // namespace camel::runtime
