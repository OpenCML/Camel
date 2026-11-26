// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * LoopView, and the commit-time check of a LOOP node against its two graphs
 * (camel/runtime/loop.h). The view reads a `GraphView`, so a frozen graph and the draft of a
 * builder answer the same questions through it; the two entry points below build the adapter.
 */

#include "camel/runtime/loop.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/utils/assert.h"
#include "graph/draft_view.h"
#include "graph/gc_graph_view.h"

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
using camel::graph::DraftGraphAdapter;
using camel::graph::FrozenGraphAdapter;
using camel::graph::GraphView;
using camel::graph::kNoRegion;
using camel::graph::LoopForm;
using camel::graph::NodeKind;
using camel::graph::PortGroup;
using camel::graph::RegionId;
using camel::graph::RegionKind;
using camel::graph::RegionRecord;

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

} // namespace

LoopView::LoopView(const GCGraph &graph, gc_node_ref_t node)
    : owned_(std::make_unique<FrozenGraphAdapter>(graph)), view_(*owned_), node_(node) {
    adopt();
}

LoopView::LoopView(const GraphDraft &draft, gc_node_ref_t node)
    : owned_(std::make_unique<DraftGraphAdapter>(draft)), view_(*owned_), node_(node) {
    adopt();
}

LoopView::LoopView(const GraphView &view, gc_node_ref_t node) : view_(view), node_(node) {
    adopt();
}

void LoopView::adopt() {
    GUARD(view_.contains(node_) && view_.kindOf(node_) == NodeKind::Loop,
          "LoopView of a node that is not a LOOP node.");
    const LoopForm form = view_.loopFormOf(node_);
    kind_               = form.kind;
    collectionCount_    = form.collectionCount;
    condRegion_         = 0;
    bodyRegion_         = 0;
    // The two parts are the regions the node owns, told apart by what they are: a LOOP names no
    // graph, so this is the whole of what it says about them (design principles 3.10).
    for (const RegionId id : view_.regionsOwnedBy(node_)) {
        const RegionRecord held = view_.region(id);
        if (held.kind == RegionKind::LoopCond) {
            condRegion_ = id;
        } else if (held.kind == RegionKind::LoopBody) {
            bodyRegion_ = id;
        }
    }
    GUARD(bodyRegion_ != 0, "A LOOP node has no body region.");
    stateCount_ = view_.inputs(node_, PortGroup::Normal).size() - (loopHasIterable(kind_) ? 1 : 0);
}

Type *LoopView::valueType() const {
    // What the node is written at. A type is interned and immutable, so the const handle the view
    // hands out is the one to read here.
    return const_cast<Type *>(view_.typeOf(node_));
}

Type *LoopView::stateType(size_t j) const {
    return const_cast<Type *>(view_.typeOf(bodyStateParam(j)));
}

Type *LoopView::collectionArrayType(size_t c) const {
    GUARD(c < collectionCount_, "LoopView: the loop has no such collection slot.");
    const auto *value = static_cast<const TupleType *>(valueType());
    GUARD(
        value != nullptr && value->size() == stateCount_ + collectionCount_,
        "LoopView: the loop's value type does not have the k + s components its shape says.");
    return value->typeAt(collectionValueIndex(c));
}

Type *LoopView::collectionElemType(size_t c) const {
    const Type *array = collectionArrayType(c);
    GUARD(
        array != nullptr && array->code() == TypeCode::Array,
        "LoopView: a collection slot's value type is not an array.");
    return static_cast<const ArrayType *>(array)->elemType();
}

std::optional<gc_node_ref_t> LoopView::iterable() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return view_.inputs(node_, PortGroup::Normal).front();
}

gc_node_ref_t LoopView::initialState(size_t j) const {
    GUARD(j < stateCount_, "LoopView: no such value of the state.");
    return view_.inputs(node_, PortGroup::Normal)[j + (loopHasIterable(kind_) ? 1 : 0)];
}

gc_node_ref_t LoopView::condStateParam(size_t j) const {
    GUARD(condRegion_ != 0 && j < stateCount_, "LoopView: no such condition parameter.");
    return view_.regionParameters(condRegion_)[j];
}

std::optional<gc_node_ref_t> LoopView::bodyElementParam() const {
    if (!loopHasIterable(kind_)) {
        return std::nullopt;
    }
    return view_.regionParameters(bodyRegion_).front();
}

gc_node_ref_t LoopView::bodyStateParam(size_t j) const {
    GUARD(j < stateCount_, "LoopView: no such body parameter.");
    const size_t at = j + (loopHasIterable(kind_) ? 1 : 0);
    return view_.regionParameters(bodyRegion_)[at];
}

std::vector<gc_node_ref_t> LoopView::regionNodes() const {
    std::vector<gc_node_ref_t> nodes;
    const auto inParts = [&](RegionId region) {
        for (int guard = 0; region != kNoRegion && guard < 4096; ++guard) {
            if ((condRegion_ != 0 && region == condRegion_) || region == bodyRegion_) {
                return true;
            }
            region = view_.region(region).parent;
        }
        return false;
    };
    for (const gc_node_ref_t ref : view_.nodes()) {
        if (inParts(view_.regionOf(ref))) {
            nodes.push_back(ref);
        }
    }
    // The slot the rounds are counted in belongs to the loop.
    nodes.push_back(roundPort());
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    return nodes;
}

std::vector<gc_node_ref_t> LoopView::externalInputs(bool constants) const {
    std::vector<gc_node_ref_t> externals;
    const std::vector<gc_node_ref_t> nodes = regionNodes();
    std::unordered_set<gc_node_ref_t> inside(nodes.begin(), nodes.end());
    std::unordered_set<gc_node_ref_t> seen;
    for (const gc_node_ref_t ref : nodes) {
        for (const PortGroup group : {PortGroup::Normal, PortGroup::Context}) {
            for (const gc_node_ref_t input : view_.inputs(ref, group)) {
                // A constant is evaluated where it is read, in place, and is not a capture of the
                // loop: a DATA of the static area (a literal, a folded literal, a static value the
                // builder materialized) or any node the graph marks `constant`, which is the one
                // question `constantOf` answers.
                const bool constant =
                    view_.kindOf(input) == NodeKind::Data || view_.constantOf(input).has_value();
                if ((constants || !constant) && !inside.contains(input) &&
                    seen.insert(input).second) {
                    externals.push_back(input);
                }
            }
        }
    }
    return externals;
}

/// The first value the LOOP takes in is what its body hands on, and the next one what its
/// condition does: the node's two `With` inputs, in that order (the third is the round port).
/// The producers that rewrite a loop keep that list current -- `test::loop_collect` writes the
/// body's new result into entry 0 -- so it is the list, not the region record, that answers;
/// the record is the fallback for a node whose parts are not wired in yet.
gc_node_ref_t LoopView::bodyResult() const {
    const std::span<const gc_node_ref_t> with = view_.inputs(node_, PortGroup::Context);
    return with.empty() ? view_.region(bodyRegion_).result : with.front();
}

gc_node_ref_t LoopView::condResult() const {
    if (condRegion_ == 0) {
        return kInvalidNodeRef;
    }
    const std::span<const gc_node_ref_t> with = view_.inputs(node_, PortGroup::Context);
    return with.size() > 1 ? with[1] : view_.region(condRegion_).result;
}
/// The value a chain of GATEs forwards, `camel/runtime/graph.h#valueBehindGates` read over the
/// view: the first ordinary input, then the first context one, to the first node that is not a
/// gate.
gc_node_ref_t LoopView::nextState() const {
    gc_node_ref_t value = bodyResult();
    for (int guard = 0; guard < 4096 && view_.contains(value) &&
                        view_.kindOf(value) == NodeKind::Gate;
         ++guard) {
        const std::span<const gc_node_ref_t> norm = view_.inputs(value, PortGroup::Normal);
        if (!norm.empty()) {
            value = norm.front();
            continue;
        }
        const std::span<const gc_node_ref_t> with = view_.inputs(value, PortGroup::Context);
        if (with.empty()) {
            return kInvalidNodeRef;
        }
        value = with.front();
    }
    return value;
}

Type *LoopView::bodyResultType() const {
    return const_cast<Type *>(view_.typeOf(nextState()));
}

slot_t loopValueComponent(const LoopView &loop, slot_t value, size_t index) {
    if (!loop.valueIsTuple()) {
        GUARD(index == 0, "A loop that does not wrap its state has no other component.");
        return value;
    }
    GUARD(
        index < loop.valueComponentCount(),
        "A component outside the loop's value tuple was asked for.");
    return fromSlot<Tuple *>(value)->get<slot_t>(index);
}

slot_t loopStateValue(const LoopView &loop, slot_t value, size_t j) {
    GUARD(j < loop.stateCount(), "The loop has no such state value.");
    return loopValueComponent(loop, value, loop.stateValueIndex(j));
}

slot_t loopCollectedValue(const LoopView &loop, slot_t value, size_t c) {
    GUARD(c < loop.collectionCount(), "The loop has no such collection slot.");
    return loopValueComponent(loop, value, loop.collectionValueIndex(c));
}

void validateRuntimeLoopInterface(
    const Program &program, const GCGraph *graph, gc_node_ref_t node) {
    const LoopKind kind = graph->loopKindOf(node);
    // A loop names no graph: the condition and the body are regions of this graph (design
    // principles 3.10), so the form it declares is exactly the regions it has.
    (void)program;
    const uint32_t condRegion = graph->loopRegionOf(node, GCRegionKind::LoopCond);
    const uint32_t bodyRegion = graph->loopRegionOf(node, GCRegionKind::LoopBody);
    if (bodyRegion == 0 || (loopHasCondition(kind) != (condRegion != 0))) {
        loopFails(graph, node, "its parts do not match its form");
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
        return graph->regionPorts(bodyRegion);
    };
    const auto condPorts = [&]() -> std::span<const gc_node_ref_t> {
        return graph->regionPorts(condRegion);
    };
    const auto bodyType = [&](gc_node_ref_t port) { return graph->node(port)->dataType(); };
    const auto condType = [&](gc_node_ref_t port) { return graph->node(port)->dataType(); };

    // I19: the ports. Both parts take the k values of the state (the body the element first); a loop
    // captures nothing, because its parts read the values where they are (one frame), and its With
    // inputs are the results of the parts and the round.
    if (m != (condRegion != 0 ? 3u : 2u)) {
        loopFails(graph, node, "a loop has the results of its parts and its round as With inputs");
    }
    if (bodyPorts().size() != iterableAt + k) {
        loopFails(
            graph,
            node,
            std::format(
                "its body takes {} values, the node passes {}",
                bodyPorts().size(),
                iterableAt + k));
    }
    if (condRegion != 0 && condPorts().size() != k) {
        loopFails(
            graph,
            node,
            std::format("its condition takes {} values, the node passes {}", condPorts().size(), k));
    }

    // I18: the types. The state types are those of the body's state ports; the condition's
    // ports agree with them; each first value can be assigned to its state; the body returns
    // the state; the condition returns bool; the element port takes the array's elements.
    const bool hasCondPart = condRegion != 0;
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
        // values first, so a consumer that ignores the tapes reads the state in the slots it looks
        // at.
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
    Type *bodyExit = graph->node(LoopView(*graph, node).nextState())->dataType();
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
        Type *condExit = graph->node(LoopView(*graph, node).condResult())->dataType();
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
            element = static_cast<ArrayType *>(sequence)->firstDimensionElement();
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
        // The operator whose append is rewritten: its id is well known to the table
        // (`oper::AppendOwned`), so the node is recognised by the id it holds. The rewrite below
        // still names the plain form by URI -- `retargetOperNode` takes no id.
        if (reinterpret_cast<const GCOperBody *>(payload.data())->oper != camel::core::oper::AppendOwned) {
            continue;
        }
        body.retargetOperNode(ref, ":struct/append_arr");
        ++retargeted;
    }
    return retargeted;
}

} // namespace camel::runtime
