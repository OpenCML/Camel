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
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * LoopView, the effect summary of a graph, and the commit-time check of a LOOP node against
 * its two graphs (camel/runtime/loop.h).
 */

#include "camel/runtime/loop.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/assert.h"

#include <format>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace camel::runtime {

using namespace camel::core::type;
using camel::core::loopHasCondition;
using camel::core::loopHasIterable;

namespace {

/// The capture ports of a loop graph: its with-ports once it is called (the builder makes the
/// loop graphs so), its closure ports otherwise.
std::span<const gc_node_ref_t> capturePortsOf(const GCGraph *graph) {
    return graph->withPorts().empty() ? graph->closureNodes() : graph->withPorts();
}

struct EffectWalk {
    std::unordered_map<const GCGraph *, EffectSummary> done;
    std::unordered_set<const GCGraph *> active;

    EffectSummary of(const GCGraph *graph) {
        if (graph == nullptr) {
            return {};
        }
        if (auto it = done.find(graph); it != done.end()) {
            return it->second;
        }
        if (!active.insert(graph).second) {
            // A graph that reaches itself: a recursion, which may not end. What it does
            // besides is what the walk that is under way collects.
            EffectSummary recursion;
            recursion.mayNotTerminate = true;
            return recursion;
        }
        const auto &table = camel::core::OperatorTable::instance();
        EffectSummary summary;
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            switch (node->kind) {
            case GCNodeKind::Oper: {
                const auto oper = graph->nodeBodyAs<GCOperBody>(ref)->oper;
                const auto traits = table.find(oper);
                summary.external |= table.hasExternalEffect(oper);
                summary.writesVariables |= table.isAssignment(oper);
                summary.mayFault |= !traits || traits->mayFault;
                if (traits && traits->callsGraphs) {
                    // It runs the function values it receives: not known here.
                    summary.external        = true;
                    summary.mayNotTerminate = true;
                }
                break;
            }
            case GCNodeKind::Call:
                summary.external        = true;
                summary.mayFault        = true;
                summary.mayNotTerminate = true;
                break;
            case GCNodeKind::Func:
                summary |= of(graph->directCalleeGraphOf(ref));
                break;
            case GCNodeKind::Loop:
                summary |= of(graph->loopCondGraphOf(ref));
                summary |= of(graph->loopBodyGraphOf(ref));
                summary.mayFault        = true;
                summary.mayNotTerminate = true;
                break;
            default:
                break;
            }
        }
        active.erase(graph);
        done.emplace(graph, summary);
        return summary;
    }
};

[[noreturn]] void loopFails(const GCGraph *graph, gc_node_ref_t node, const std::string &why) {
    throw std::runtime_error(
        std::format("Runtime graph '{}' has LOOP node {}: {}.", graph->name(), node, why));
}

std::string typeText(const Type *type) { return type ? type->toString() : std::string("none"); }

} // namespace

EffectSummary effectSummaryOf(const GCGraph *graph) { return EffectWalk{}.of(graph); }

LoopView::LoopView(const GCGraph &graph, gc_node_ref_t node) : graph_(&graph), node_(node) {
    const GCNode *record = graph.node(node);
    ASSERT(record != nullptr && record->kind == GCNodeKind::Loop, "LoopView of a node that is not a LOOP node.");
    kind_       = graph.loopKindOf(node);
    cond_       = graph.loopCondGraphOf(node);
    body_       = graph.loopBodyGraphOf(node);
    stateCount_ = graph.normInputsOf(node).size() - (loopHasIterable(kind_) ? 1 : 0);
}

Type *LoopView::stateType(size_t j) const {
    return body_->node(bodyStateParam(j))->dataType();
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
    const gc_node_ref_t result = nextState();
    const GCNode *node         = body_->node(result);
    if (stateCount_ < 2 || node == nullptr || node->kind != GCNodeKind::Fill) {
        return std::nullopt;
    }
    const auto *fill = body_->nodeBodyAs<GCFillBody>(result);
    if (fill->fillKind != GCFillKind::Tuple) {
        return std::nullopt;
    }
    // A FILL copies its template (Norm input 0) and writes With input i into position
    // slots()[i]; a position no input writes keeps the template's constant.
    const auto slots  = fill->slots();
    const auto inputs = body_->withInputsOf(result);
    for (size_t i = 0; i < slots.size() && i < inputs.size(); ++i) {
        if (static_cast<size_t>(slots[i]) == j) {
            return inputs[i];
        }
    }
    return std::nullopt;
}

EffectSummary LoopView::effects() const {
    EffectWalk walk;
    EffectSummary summary   = walk.of(cond_);
    summary |= walk.of(body_);
    summary.mayFault        = true;
    summary.mayNotTerminate = true;
    return summary;
}

void validateRuntimeLoopInterface(const GCGraph *graph, gc_node_ref_t node) {
    const LoopKind kind = graph->loopKindOf(node);
    const GCGraph *cond = graph->loopCondGraphOf(node);
    const GCGraph *body = graph->loopBodyGraphOf(node);
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
    Type *valueType = graph->node(node)->dataType();
    Type *expected  = k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));
    Type *bodyExit  = body->funcType() ? body->funcType()->exitType() : nullptr;
    if (bodyExit == nullptr || !expected->assignableFrom(bodyExit)) {
        loopFails(
            graph,
            node,
            std::format(
                "its body graph returns a '{}', the state is a '{}'",
                typeText(bodyExit),
                typeText(expected)));
    }
    if (valueType == nullptr || !valueType->equals(expected)) {
        loopFails(
            graph,
            node,
            std::format(
                "its value is a '{}', the state is a '{}'",
                typeText(valueType),
                typeText(expected)));
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

} // namespace camel::runtime
