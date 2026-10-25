// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include <algorithm>
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

/// A constant: a value known at build time. A DATA node (a literal, a folded literal, a static
/// value the builder materialized) is one, and so is any node the graph marks `constant`. A
/// constant is evaluated where it is read, in place, and is not a capture of a LOOP: a consumer
/// that lifts the loop into another representation needs no input for it (design principles 3.10;
/// "a constant is not a capture").
bool constantValue(const GCNode &node) {
    return node.kind == GCNodeKind::Data || node.isConstant();
}

/// The same for a node of a draft, whose flags are read from its header.
bool constantValue(const DraftNodeHeader &header) {
    return header.kind == GCNodeKind::Data || (header.runtimeFlags & kGCNodeFlagConstant) != 0;
}

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
    (void)program;
    [[maybe_unused]] const DraftNodeHeader *header = draft.header(node);
    ASSERT(
        header != nullptr && header->kind == GCNodeKind::Loop,
        "LoopDraftView of a node that is not a LOOP node.");
    const GCLoopBody body = draft.loopGraphsOf(node);
    kind_                 = body.kind;
    condRegion_           = draft.loopRegionOf(node, GCRegionKind::LoopCond);
    bodyRegion_           = draft.loopRegionOf(node, GCRegionKind::LoopBody);
    ASSERT(bodyRegion_ != 0, "A LOOP node has no body region.");
    stateCount_      = draft.normInputsOf(node).size() - (loopHasIterable(kind_) ? 1 : 0);
    collectionCount_ = body.collectionCount;
}

Type *LoopDraftView::valueType() const { return draft_->header(node_)->dataType; }

Type *LoopDraftView::stateType(size_t j) const {
    return draft_->header(bodyStateParam(j))->dataType;
}

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

std::vector<gc_node_ref_t> LoopDraftView::regionNodes() const {
    const auto inParts = [&](uint32_t region) {
        for (int guard = 0; region != 0 && guard < 4096; ++guard) {
            if ((condRegion_ != 0 && region == condRegion_) || region == bodyRegion_) {
                return true;
            }
            region = draft_->region(region).parent;
        }
        return false;
    };
    std::vector<gc_node_ref_t> nodes;
    for (gc_node_ref_t ref = 0; ref < draft_->nodeSlotCount(); ++ref) {
        if (draft_->alive(ref) && inParts(draft_->nodeRegion(ref))) {
            nodes.push_back(ref);
        }
    }
    nodes.push_back(draft_->withInputsOf(node_).back()); // the round slot
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    return nodes;
}

std::vector<gc_node_ref_t> LoopDraftView::externalInputs(bool constants) const {
    const std::vector<gc_node_ref_t> nodes = regionNodes();
    std::unordered_set<gc_node_ref_t> inside(nodes.begin(), nodes.end());
    std::unordered_set<gc_node_ref_t> seen;
    std::vector<gc_node_ref_t> externals;
    for (gc_node_ref_t ref : nodes) {
        for (auto inputs : {draft_->normInputsOf(ref), draft_->withInputsOf(ref)}) {
            for (gc_node_ref_t input : inputs) {
                if ((constants || !constantValue(*draft_->header(input))) &&
                    !inside.contains(input) && seen.insert(input).second) {
                    externals.push_back(input);
                }
            }
        }
    }
    return externals;
}

gc_node_ref_t LoopDraftView::condStateParam(size_t j) const {
    ASSERT(condRegion_ != 0 && j < stateCount_, "LoopDraftView: no such condition parameter.");
    return draft_->regionPortsOf(condRegion_)[j];
}

std::optional<gc_node_ref_t> LoopDraftView::bodyElementParam() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return draft_->regionPortsOf(bodyRegion_).front();
}

gc_node_ref_t LoopDraftView::bodyStateParam(size_t j) const {
    ASSERT(j < stateCount_, "LoopDraftView: no such body parameter.");
    const size_t at = j + (loopHasIterable(kind_) ? 1 : 0);
    return draft_->regionPortsOf(bodyRegion_)[at];
}

gc_node_ref_t LoopDraftView::bodyResult() const { return draft_->region(bodyRegion_).result; }

gc_node_ref_t LoopDraftView::nextState() const {
    gc_node_ref_t at = bodyResult();
    for (int guard = 0; guard < 64; ++guard) {
        const auto *header = draft_->header(at);
        if (header == nullptr || header->kind != GCNodeKind::Gate ||
            draft_->normInputsOf(at).empty()) {
            break;
        }
        at = draft_->normInputsOf(at).front();
    }
    return at;
}

Type *LoopDraftView::bodyResultType() const { return draft_->header(nextState())->dataType; }

LoopView::LoopView(const Program &program, const GCGraph &graph, gc_node_ref_t node)
    : program_(&program), graph_(&graph), node_(node) {
    [[maybe_unused]] const GCNode *record = graph.node(node);
    ASSERT(
        record != nullptr && record->kind == GCNodeKind::Loop,
        "LoopView of a node that is not a LOOP node.");
    kind_       = graph.loopKindOf(node);
    condRegion_ = graph.loopRegionOf(node, GCRegionKind::LoopCond);
    bodyRegion_ = graph.loopRegionOf(node, GCRegionKind::LoopBody);
    ASSERT(bodyRegion_ != 0, "A LOOP node has no body region.");
    stateCount_      = graph.normInputsOf(node).size() - (loopHasIterable(kind_) ? 1 : 0);
    collectionCount_ = graph.loopCollectionCountOf(node);
}

Type *LoopView::stateType(size_t j) const { return graph_->node(bodyStateParam(j))->dataType(); }

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
    ASSERT(condRegion_ != 0 && j < stateCount_, "LoopView: no such condition parameter.");
    return graph_->regionPorts(condRegion_)[j];
}

std::optional<gc_node_ref_t> LoopView::bodyElementParam() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return graph_->regionPorts(bodyRegion_).front();
}

gc_node_ref_t LoopView::bodyStateParam(size_t j) const {
    ASSERT(j < stateCount_, "LoopView: no such body parameter.");
    const size_t at = j + (loopHasIterable(kind_) ? 1 : 0);
    return graph_->regionPorts(bodyRegion_)[at];
}

std::vector<gc_node_ref_t> LoopView::regionNodes() const {
    std::vector<gc_node_ref_t> nodes;
    const auto inParts = [&](uint32_t region) {
        for (int guard = 0; region != 0 && guard < 4096; ++guard) {
            if ((region == condRegion_ && condRegion_ != 0) || region == bodyRegion_) {
                return true;
            }
            const GCRegion *held = graph_->region(region);
            region               = held != nullptr ? held->parent : 0;
        }
        return false;
    };
    for (gc_node_ref_t ref = 0; ref < graph_->nodeCount(); ++ref) {
        if (graph_->containsNodeRef(ref) && graph_->node(ref) != nullptr &&
            inParts(graph_->nodeRegion(ref))) {
            nodes.push_back(ref);
        }
    }
    // The slot the rounds are counted in belongs to the loop.
    nodes.push_back(roundPort());
    std::sort(nodes.begin(), nodes.end());
    return nodes;
}

std::vector<gc_node_ref_t> LoopView::externalInputs(bool constants) const {
    std::vector<gc_node_ref_t> externals;
    const std::vector<gc_node_ref_t> nodes = regionNodes();
    std::unordered_set<gc_node_ref_t> inside(nodes.begin(), nodes.end());
    std::unordered_set<gc_node_ref_t> seen;
    for (gc_node_ref_t ref : nodes) {
        for (auto inputs : {graph_->normInputsOf(ref), graph_->withInputsOf(ref)}) {
            for (gc_node_ref_t input : inputs) {
                const GCNode *node = graph_->node(input);
                if (node != nullptr && (constants || !constantValue(*node)) &&
                    !inside.contains(input) &&
                    seen.insert(input).second) {
                    externals.push_back(input);
                }
            }
        }
    }
    return externals;
}

gc_node_ref_t LoopView::bodyResult() const { return graph_->withInputsOf(node_)[0]; }

gc_node_ref_t LoopView::condResult() const {
    return condRegion_ != 0 ? graph_->withInputsOf(node_)[1] : kInvalidNodeRef;
}

gc_node_ref_t LoopView::nextState() const {
    // The value behind the GATEs that order it after the statements of the body.
    gc_node_ref_t at = bodyResult();
    for (int guard = 0; guard < 64; ++guard) {
        const GCNode *node = graph_->node(at);
        if (node == nullptr || node->kind != GCNodeKind::Gate || graph_->normInputsOf(at).empty()) {
            break;
        }
        at = graph_->normInputsOf(at).front();
    }
    return at;
}

Type *LoopView::bodyResultType() const { return graph_->node(nextState())->dataType(); }

EffectSummary LoopView::condEffects() const {
    return effectSummaryOfRegion(*program_, graph_, condRegion_);
}

EffectSummary LoopView::bodyEffects() const {
    return effectSummaryOfRegion(*program_, graph_, bodyRegion_);
}

std::optional<gc_node_ref_t> LoopView::nextStateComponent(size_t j) const {
    if (!nextStateIsTuple()) {
        return std::nullopt;
    }
    return fillComponent(graph_, nextState(), j);
}

std::optional<gc_node_ref_t> LoopView::nextCollectionComponent(size_t c) const {
    if (collectionCount_ == 0 || c >= collectionCount_) {
        return std::nullopt;
    }
    return fillComponent(graph_, nextState(), collectionValueIndex(c));
}

EffectSummary LoopView::effects() const {
    EffectSummary summary = condEffects();
    summary |= bodyEffects();
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
    const bool regions  = body == nullptr;
    const uint32_t condRegion = regions ? graph->loopRegionOf(node, GCRegionKind::LoopCond) : 0;
    const uint32_t bodyRegion = regions ? graph->loopRegionOf(node, GCRegionKind::LoopBody) : 0;
    if (regions ? (bodyRegion == 0 || (loopHasCondition(kind) != (condRegion != 0)))
                : (loopHasCondition(kind) != (cond != nullptr))) {
        loopFails(graph, node, "its parts do not match its form");
    }
    if (!regions && (!body->hasNodePayload() || (cond && !cond->hasNodePayload()))) {
        loopFails(graph, node, "a graph of the loop is not encoded");
    }
    const auto norm         = graph->normInputsOf(node);
    const size_t iterableAt = loopHasIterable(kind) ? 1 : 0;
    if (norm.size() <= iterableAt) {
        loopFails(graph, node, "it has no state");
    }
    const size_t k = norm.size() - iterableAt;
    const size_t m = graph->withInputsOf(node).size();

    // The ports of the two parts: the element (body) and the k values of the state, as the ports
    // of the part's graph or of its region.
    const auto bodyPorts = [&]() -> std::span<const gc_node_ref_t> {
        return regions ? graph->regionPorts(bodyRegion) : body->normPorts();
    };
    const auto condPorts = [&]() -> std::span<const gc_node_ref_t> {
        return regions ? graph->regionPorts(condRegion) : cond->normPorts();
    };
    const auto bodyType = [&](gc_node_ref_t port) {
        return (regions ? graph : body)->node(port)->dataType();
    };
    const auto condType = [&](gc_node_ref_t port) {
        return (regions ? graph : cond)->node(port)->dataType();
    };

    // I19: the ports. Both parts take the k values of the state (the body the element first)
    // and, in graph form, the m captures, in the order of the node's With inputs. A loop in
    // region form captures nothing: its parts read the values where they are, and its With inputs
    // are the results of the parts.
    if (regions && m != (condRegion != 0 ? 3u : 2u)) {
        loopFails(
            graph,
            node,
            "a loop in region form has the results of its parts and its round as With inputs");
    }
    if (bodyPorts().size() != iterableAt + k || (!regions && capturePortsOf(body).size() != m)) {
        loopFails(
            graph,
            node,
            std::format(
                "its body takes {} values and {} captures, the node passes {} and {}",
                bodyPorts().size(),
                regions ? 0 : capturePortsOf(body).size(),
                iterableAt + k,
                m));
    }
    if ((regions ? condRegion != 0 : cond != nullptr) &&
        (condPorts().size() != k || (!regions && capturePortsOf(cond).size() != m))) {
        loopFails(
            graph,
            node,
            std::format(
                "its condition takes {} values and {} captures, the node passes {} and {}",
                condPorts().size(),
                regions ? 0 : capturePortsOf(cond).size(),
                k,
                m));
    }
    for (size_t c = 0; !regions && cond && c < m; ++c) {
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
    const bool hasCondPart = regions ? condRegion != 0 : cond != nullptr;
    std::vector<Type *> stateTypes;
    for (size_t j = 0; j < k; ++j) {
        stateTypes.push_back(bodyType(bodyPorts()[iterableAt + j]));
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
        if (hasCondPart) {
            Type *condPortType = condType(condPorts()[j]);
            if (condPortType != stateTypes[j] &&
                !(condPortType && stateTypes[j] && condPortType->equals(stateTypes[j]))) {
                loopFails(
                    graph,
                    node,
                    std::format(
                        "state {} is a '{}' in the condition and a '{}' in the body",
                        j,
                        typeText(condPortType),
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
    Type *bodyExit = regions ? graph->node(LoopView(program, *graph, node).nextState())->dataType()
                             : (body->funcType() ? body->funcType()->exitType() : nullptr);
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
    if (hasCondPart) {
        Type *condExit =
            regions ? graph->node(LoopView(program, *graph, node).condResult())->dataType()
                    : (cond->funcType() ? cond->funcType()->exitType() : nullptr);
        if (condExit == nullptr || condExit->code() != TypeCode::Bool) {
            loopFails(
                graph,
                node,
                std::format("its condition graph returns a '{}', not a bool", typeText(condExit)));
        }
    }
    if (iterableAt == 1) {
        Type *sequence = graph->node(norm.front())->dataType();
        const bool intervalIterable =
            graph->loopIterableFormOf(node) == camel::core::LoopIterableForm::Interval;
        Type *element = nullptr;
        if (intervalIterable) {
            // The sequence is the pair of the interval's two bounds, not an array of its
            // elements: `for i in a..b` is fused by the compiler (design principles 4.2).
            const auto *bounds = sequence != nullptr && sequence->code() == TypeCode::Tuple
                                     ? static_cast<const TupleType *>(sequence)
                                     : nullptr;
            if (bounds == nullptr || bounds->size() != 2 || bounds->typeAt(0) == nullptr ||
                bounds->typeAt(1) == nullptr || bounds->typeAt(0)->code() != TypeCode::Int64 ||
                bounds->typeAt(1)->code() != TypeCode::Int64) {
                loopFails(
                    graph,
                    node,
                    std::format(
                        "it iterates an interval written as a '{}', not as its two integers",
                        typeText(sequence)));
            }
            element = Type::Int64();
        } else {
            if (sequence == nullptr || sequence->code() != TypeCode::Array) {
                loopFails(graph, node, std::format("it iterates over a '{}'", typeText(sequence)));
            }
            element = static_cast<ArrayType *>(sequence)->elemType();
        }
        Type *port = bodyType(bodyPorts().front());
        if (port == nullptr || element == nullptr || !port->assignableFrom(element)) {
            loopFails(
                graph,
                node,
                std::format(
                    "its body takes elements of type '{}' from {} of '{}'",
                    typeText(port),
                    intervalIterable ? "an interval" : "an array",
                    typeText(element)));
        }
    }
}

size_t fallBackToCopyingAppend(GraphDraft &body, uint32_t region) {
    size_t retargeted = 0;
    for (size_t id = 0; id < body.nodeSlotCount(); ++id) {
        const auto ref     = static_cast<gc_node_ref_t>(id);
        const DraftNode *n = body.node(ref);
        if (n == nullptr || n->header.kind != GCNodeKind::Oper) {
            continue;
        }
        if (region != 0 && !body.regionWithin(body.nodeRegion(ref), region)) {
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
