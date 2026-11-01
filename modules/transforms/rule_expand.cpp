// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "rule_expand.h"

#include "camel/core/rtdata/func.h"
#include "camel/core/type/base.h"

#include <algorithm>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

namespace rt = camel::runtime;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace {

/// Whether a rule's expansion records how many of the arguments of the call it produces are tangents
/// (`addFuncNode`'s last argument, `GCNode::tangentArgs`). On, the fact lives on the node and
/// `transpose` reads it there. Off, the call records nothing, and the transpose of a kernel inside an
/// arm stops again at the tangent that has no value -- the A/B of the two readings.
constexpr bool kRecordTangentArgs = true;

/// The graph a rule's right side answers with, read from the right side itself.
///
/// The right side of `x * w => (dx, dw) => mul_tangent(x, w, dx, dw)` is a graph that builds the
/// closure `(dx, dw) => mul_tangent(x, w, dx, dw)` over the values the left side bound: its exit is
/// a FILL of the `FunctionClosure` kind, whose static input names the graph the closure runs, and
/// whose `with` inputs are the captured values, in slot order. That inner graph is the body a call
/// of the closure would run, and its two port lists say what the call would pass:
///
///   `normPorts`    the parameters -- the tangents, `dx, dw`;
///   `closureNodes` the captures -- the values the left side bound, `x, w`.
///
/// Reading the body from here is what keeps the argument order out of C++: `mul_tangent(x, w, dx,
/// dw)` is the order the body's own call node already has, not a rule about how a right side is
/// written.
struct RuleRightSide {
    /// The body, a graph of the instance's own program.
    const camel::runtime::GCGraph *body = nullptr;
    /// The values the closure captures, as nodes of the right side's graph, in slot order.
    std::vector<gc_node_ref_t> captures;
};

/// Fills `found` from the instance's right side, or says in `why` what about it is not a function of
/// the tangents.
bool findRuleRightSide(
    const camel::compile::gir::GenericRuleInstance &instance, RuleRightSide &found,
    std::string &why) {
    const rt::GCGraph *rhs = instance.rhs;
    // The exit node is the graph's value-producing node; a gate may stand in front of it when the
    // value is carried out of a block.
    gc_node_ref_t fill = rhs->exitNodeRef();
    for (size_t guard = 0; guard < 8 && fill != rt::kInvalidNodeRef; ++guard) {
        const rt::GCNode *node = rhs->node(fill);
        if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
            break;
        }
        const auto carried = rhs->normInputsOf(fill);
        fill = carried.empty() ? rt::kInvalidNodeRef : carried.front();
    }
    const rt::GCNode *exit = fill == rt::kInvalidNodeRef ? nullptr : rhs->node(fill);
    if (exit == nullptr || exit->kind != rt::GCNodeKind::Fill) {
        why = "the rule's right side does not answer with a function of the tangents";
        return false;
    }
    const auto *fillBody = rhs->nodeBodyAs<rt::GCFillBody>(fill);
    if (fillBody == nullptr || fillBody->fillKind != rt::GCFillKind::FunctionClosure) {
        why = "the rule's right side does not answer with a function of the tangents";
        return false;
    }
    // The graph the closure runs is the static function value the FILL fills its slots with.
    const std::span<const slot_t> slots = rhs->staticSlots();
    for (gc_node_ref_t input : rhs->normInputsOf(fill)) {
        const rt::GCNode *node = rhs->node(input);
        if (node == nullptr || node->kind != rt::GCNodeKind::Data || node->dataIndex >= 0) {
            continue;
        }
        const size_t at = static_cast<size_t>(-static_cast<std::ptrdiff_t>(node->dataIndex));
        if (at >= slots.size()) {
            continue;
        }
        const auto *function = camel::core::rtdata::fromSlot<const ::Function *>(slots[at]);
        if (function == nullptr) {
            continue;
        }
        found.body = instance.program->graph(function->graphId());
        if (found.body == nullptr) {
            break;
        }
        for (gc_node_ref_t capture : rhs->withInputsOf(fill)) {
            found.captures.push_back(capture);
        }
        return true;
    }
    why = "the rule's right side names no graph for the function it answers with";
    return false;
}

} // namespace

/// The rule's right side expanded at the match site: the body of the closure it answers with is
/// copied into this graph, its captures bound to the operands and its parameters to the tangents,
/// and the copy's exit is the operator's derivative.
///
/// This is one lambda layer, not a general inliner. A body that is more than a straight line of
/// computations -- a region, an indirect call, a function value -- is refused by name, the way a
/// primitive with no rule is: the transform has no reading for it, and a copy that guessed would
/// compute something else.
gc_node_ref_t expandRuleRightSide(
    camel::runtime::GraphDraftGroup &group, camel::runtime::GraphTransaction &transaction,
    const camel::compile::gir::GenericRuleInstance &instance,
    const std::vector<gc_node_ref_t> &values, const std::vector<gc_node_ref_t> &tangents,
    std::string &why) {
    RuleRightSide right;
    if (!findRuleRightSide(instance, right, why)) {
        return rt::kInvalidNodeRef;
    }
    // The body's calls name graphs of the instance's own program, and a FUNC node reaches its
    // callee through this program's table: the graphs the rule was instantiated into belong to the
    // program this lift builds on, the way a callee the lifted function calls does.
    group.adopt(*instance.program);
    const rt::GCGraph *body      = right.body;
    const auto capturePorts     = body->closureNodes();
    const auto parameterPorts   = body->normPorts();
    if (capturePorts.size() != right.captures.size() || parameterPorts.size() != tangents.size()) {
        why = "the rule's right side does not take the values and tangents the rule binds";
        return rt::kInvalidNodeRef;
    }
    // A capture is a port of the right side's graph, which is one of the arguments the rule is
    // called with: the operands, in the order the left side writes them.
    const auto rhsPorts = instance.rhs->normPorts();
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> bound;
    for (size_t i = 0; i < capturePorts.size(); ++i) {
        const auto at = std::find(rhsPorts.begin(), rhsPorts.end(), right.captures[i]);
        if (at == rhsPorts.end()) {
            why = "a value the rule's right side captures is not one of its parameters";
            return rt::kInvalidNodeRef;
        }
        const size_t argument = static_cast<size_t>(at - rhsPorts.begin());
        if (argument >= values.size()) {
            why = "the rule's right side captures more values than the rule binds";
            return rt::kInvalidNodeRef;
        }
        bound[capturePorts[i]] = values[argument];
    }
    for (size_t i = 0; i < parameterPorts.size(); ++i) {
        bound[parameterPorts[i]] = tangents[i];
    }

    auto &draft                                = transaction.edit();
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> copied;
    const std::span<const slot_t> staticSlots = body->staticSlots();
    for (gc_node_ref_t ref = 0; ref < body->nodeCount(); ++ref) {
        const rt::GCNode *node = body->node(ref);
        if (node == nullptr) {
            continue;
        }
        if (node->kind == rt::GCNodeKind::Port) {
            // A port of the body is a value the call passes: bound, never copied, so the copy has
            // no parameters of its own.
            const auto at = bound.find(ref);
            if (at == bound.end()) {
                why = "the rule's right side reads a value that is neither a capture nor a tangent";
                return rt::kInvalidNodeRef;
            }
            copied[ref] = at->second;
            continue;
        }
        if (body->nodeRegion(ref) != 0) {
            why = "the rule's right side is not a straight line: it has a region";
            return rt::kInvalidNodeRef;
        }
        std::vector<gc_node_ref_t> inputs;
        for (gc_node_ref_t input : body->normInputsOf(ref)) {
            const auto at = copied.find(input);
            if (at == copied.end()) {
                why = "the rule's right side uses a node written after it";
                return rt::kInvalidNodeRef;
            }
            inputs.push_back(at->second);
        }
        if (!body->withInputsOf(ref).empty()) {
            why = "the rule's right side passes a context argument of its own";
            return rt::kInvalidNodeRef;
        }
        const bool isFunctionValue =
            node->dataType() != nullptr &&
            node->dataType()->code() == camel::core::type::TypeCode::Function;
        switch (node->kind) {
        case rt::GCNodeKind::Data: {
            // A constant of the body: the same value, in this graph. A function value is not a
            // constant to copy -- it is the closure this expansion exists to avoid.
            if (isFunctionValue) {
                why = "the rule's right side answers with a function value";
                return rt::kInvalidNodeRef;
            }
            const std::ptrdiff_t index = -static_cast<std::ptrdiff_t>(node->dataIndex);
            if (node->dataIndex >= 0 || index < 0 ||
                static_cast<size_t>(index) >= staticSlots.size()) {
                why = "the rule's right side reads a value that is not a constant of its graph";
                return rt::kInvalidNodeRef;
            }
            copied[ref] = draft.materializeStaticValue(
                staticSlots[static_cast<size_t>(index)], node->dataType(),
                rt::kGCNodeFlagConstant);
            break;
        }
        case rt::GCNodeKind::Oper: {
            const auto *oper = body->nodeBodyAs<rt::GCOperBody>(ref);
            if (oper == nullptr) {
                why = "the rule's right side has an operator node with no operator";
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t made = draft.addOperNode(node->dataType(), nullptr, oper->uri());
            if (!inputs.empty()) {
                draft.setNormInputs(made, inputs);
            }
            copied[ref] = made;
            break;
        }
        case rt::GCNodeKind::Func: {
            // A call to a named function: what the rule's right side is for, and what stays a call.
            if (isFunctionValue) {
                why = "the rule's right side answers with a function value";
                return rt::kInvalidNodeRef;
            }
            const GraphId callee = body->calleeIdOf(ref);
            if (!rt::isValid(callee)) {
                why = "the rule's right side calls a function it does not name";
                return rt::kInvalidNodeRef;
            }
            // How many of the call's arguments are tangents -- the rule's own split, recorded on the
            // call the rule produced. The right side binds the values as its captures and takes the
            // tangents as its own parameters (`x * w => (dx, dw) => mul_tangent(x, w, dx, dw)`), so an
            // argument that is one of this expansion's tangent ports is a tangent, and the count is
            // read off the rule. This is the one place the fact exists in the tree: `transpose` reads
            // the node rather than re-deriving the split from a name, and there is no second table to
            // keep in step when a rule is added.
            uint16_t tangentArgs = 0;
            if (kRecordTangentArgs && !inputs.empty() && !tangents.empty()) {
                for (size_t i = inputs.size(); i-- > 0;) {
                    if (std::find(tangents.begin(), tangents.end(), inputs[i]) == tangents.end()) {
                        break;
                    }
                    ++tangentArgs;
                }
            }
            const gc_node_ref_t made = draft.addFuncNode(
                callee, node->dataType(), 0, draft.allocateRuntimeSlot(node->dataType()), 0,
                tangentArgs);
            if (!inputs.empty()) {
                draft.setNormInputs(made, inputs);
            }
            // The call reaches its callee through this node, so that graph is a dependency of this
            // one by the reference table's own rule (a FUNC node names its callee).
            draft.addDependency(callee);
            copied[ref] = made;
            break;
        }
        case rt::GCNodeKind::Gate: {
            const gc_node_ref_t made =
                draft.addGateNode(node->dataType(), draft.allocateRuntimeSlot(node->dataType()));
            if (!inputs.empty()) {
                draft.setNormInputs(made, inputs);
            }
            copied[ref] = made;
            break;
        }
        default:
            why = "the rule's right side is not a straight line of computations";
            return rt::kInvalidNodeRef;
        }
    }
    // The body's exit is the value the call of the closure would answer with; a gate in front of it
    // carries the same value.
    gc_node_ref_t out = body->exitNodeRef();
    for (size_t guard = 0; guard < 8; ++guard) {
        const rt::GCNode *node = body->node(out);
        if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
            break;
        }
        const auto carried = body->normInputsOf(out);
        if (carried.empty()) {
            break;
        }
        out = carried.front();
    }
    const auto at = copied.find(out);
    if (at == copied.end()) {
        why = "the rule's right side has no exit the transform can read";
        return rt::kInvalidNodeRef;
    }
    return at->second;
}

} // namespace camel::transforms