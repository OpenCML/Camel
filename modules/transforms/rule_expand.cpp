// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "rule_expand.h"

#include "camel/core/rtdata/func.h"
#include "camel/core/type/base.h"

#include <algorithm>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

namespace rt = camel::runtime;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace {

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
    gc_node_ref_t exitRef = rhs->exitNodeRef();
    for (size_t guard = 0; guard < 8 && exitRef != rt::kInvalidNodeRef; ++guard) {
        const rt::GCNode *node = rhs->node(exitRef);
        if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
            break;
        }
        const auto carried = rhs->normInputsOf(exitRef);
        exitRef = carried.empty() ? rt::kInvalidNodeRef : carried.front();
    }
    const rt::GCNode *exit = exitRef == rt::kInvalidNodeRef ? nullptr : rhs->node(exitRef);
    if (exit == nullptr) {
        why = "the rule's right side has no value to answer with";
        return false;
    }
    // A right side answers a *function of the tangents*, and there are two shapes of that answer.
    // One captures the operands: a FILL of the FunctionClosure kind, whose static input names the
    // graph the closure runs and whose with inputs are the captured values. One captures nothing --
    // a rule whose answer is a function of the tangent alone -- and then there is nothing to carry
    // with the value, so the builder hands over the function value itself (`builder_calls.cpp`:
    // "if (hasClosure)"), a Data node over a static function slot. Same graph, no wrapper.
    gc_node_ref_t functionNode = rt::kInvalidNodeRef;
    if (exit->kind == rt::GCNodeKind::Fill) {
        const auto *fillBody = rhs->nodeBodyAs<rt::GCFillBody>(exitRef);
        if (fillBody == nullptr || fillBody->fillKind != rt::GCFillKind::FunctionClosure) {
            why = std::string("the rule's right side ends in a fill of kind ") +
                  (fillBody == nullptr ? std::string("?")
                                       : std::to_string(static_cast<int>(fillBody->fillKind))) +
                  ", not a function of the tangents";
            return false;
        }
        for (gc_node_ref_t input : rhs->normInputsOf(exitRef)) {
            const rt::GCNode *node = rhs->node(input);
            if (node != nullptr && node->kind == rt::GCNodeKind::Data && node->dataIndex < 0) {
                functionNode = input;
                break;
            }
        }
        for (gc_node_ref_t capture : rhs->withInputsOf(exitRef)) {
            found.captures.push_back(capture);
        }
    } else if (exit->kind == rt::GCNodeKind::Data && exit->dataIndex < 0) {
        functionNode = exitRef;
    } else {
        why = std::string("the rule's right side ends in a node of kind ") +
              std::to_string(static_cast<int>(exit->kind)) + ", not a function of the tangents";
        return false;
    }
    if (functionNode == rt::kInvalidNodeRef) {
        why = "the rule's right side names no graph for the function it answers with";
        return false;
    }
    // The graph the answer runs is the static function value the Data node holds.
    const std::span<const slot_t> slots = rhs->staticSlots();
    const size_t at = static_cast<size_t>(
        -static_cast<std::ptrdiff_t>(rhs->node(functionNode)->dataIndex));
    if (at < slots.size()) {
        const auto *function = camel::core::rtdata::fromSlot<const ::Function *>(slots[at]);
        if (function != nullptr) {
            found.body = instance.program->graph(function->graphId());
            if (found.body != nullptr) {
                return true;
            }
        }
    }
    why = "the rule's right side names no graph for the function it answers with";
    return false;
}

/// Which of the body's ports some node of the body reads. A port nothing reads is a value the
/// expansion must not ask for: the right side names it nowhere, and a caller that has no value for
/// it is a caller the expansion can still serve.
///
/// `jvp` never needed this (it always has the operands' values); `transpose` does. The two operands
/// of `x * c` are not both available where a loop body's own state stands in one of them, while the
/// answer that position needs reads only the other -- the constant -- and the rule says so by
/// naming only that one.
std::vector<bool> portsReadByBody(const rt::GCGraph &body, const std::vector<bool> &keep) {
    std::vector<bool> read(body.nodeCount(), false);
    for (gc_node_ref_t ref = 0; ref < body.nodeCount(); ++ref) {
        if (body.node(ref) == nullptr || (!keep.empty() && !keep[ref])) {
            continue;
        }
        for (gc_node_ref_t input : body.normInputsOf(ref)) {
            if (input < read.size()) {
                read[input] = true;
            }
        }
    }
    return read;
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
    const std::function<gc_node_ref_t(size_t)> &valueOf,
    const std::vector<gc_node_ref_t> &tangents, std::string &why, size_t position) {
    RuleRightSide right;
    if (!findRuleRightSide(instance, right, why)) {
        return rt::kInvalidNodeRef;
    }
    // The body's calls name graphs of the instance's own program, and a FUNC node reaches its
    // callee through this program's table: the graphs the rule was instantiated into belong to the
    // program this lift builds on, the way a callee the lifted function calls does.
    group.adopt(*instance.program);
    const rt::GCGraph *body      = right.body;
    // Which node of the body is *this* answer. A rule's left side names N values, and its right side
    // answers N cotangents: a tuple, in the order the left side writes them. Position `i` is that
    // tuple's i-th component, and only the nodes that component needs are copied -- so a position
    // never asks for a value its own answer does not read (a loop body's own state, for one, which
    // has no forward value at all). A right side that answers one value answers position 0 only.
    std::vector<bool> keep;
    gc_node_ref_t answerRef = body->exitNodeRef();
    if (position != kWholeAnswer) {
        for (size_t guard = 0; guard < 8; ++guard) {
            const rt::GCNode *node =
                answerRef == rt::kInvalidNodeRef ? nullptr : body->node(answerRef);
            if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
                break;
            }
            const auto carried = body->normInputsOf(answerRef);
            answerRef = carried.empty() ? rt::kInvalidNodeRef : carried.front();
        }
        const rt::GCNode *exit = answerRef == rt::kInvalidNodeRef ? nullptr : body->node(answerRef);
        if (exit == nullptr) {
            why = "the rule's right side has no value to answer with";
            return rt::kInvalidNodeRef;
        }
        if (exit->kind == rt::GCNodeKind::Fill) {
            const auto *fillBody = body->nodeBodyAs<rt::GCFillBody>(answerRef);
            if (fillBody != nullptr && fillBody->fillKind == rt::GCFillKind::Tuple) {
                const auto components = body->withInputsOf(answerRef);
                if (position >= components.size()) {
                    why = "the rule's right side answers " + std::to_string(components.size()) +
                          " name(s), and the position asked for is " + std::to_string(position);
                    return rt::kInvalidNodeRef;
                }
                answerRef = components[position];
            } else if (position != 0) {
                why = "the rule's right side answers one name, and the position asked for is " +
                      std::to_string(position);
                return rt::kInvalidNodeRef;
            }
        } else if (position != 0) {
            why = "the rule's right side answers one name, and the position asked for is " +
                  std::to_string(position);
            return rt::kInvalidNodeRef;
        }
        keep.assign(body->nodeCount(), false);
        std::vector<gc_node_ref_t> work{answerRef};
        while (!work.empty()) {
            const gc_node_ref_t ref = work.back();
            work.pop_back();
            if (ref >= keep.size() || keep[ref]) {
                continue;
            }
            keep[ref] = true;
            for (gc_node_ref_t input : body->normInputsOf(ref)) {
                work.push_back(input);
            }
            // An angle argument the answer names is part of what the answer is made of: it is copied
            // with the nodes that read it, not left behind for the copy to trip over.
            for (gc_node_ref_t input : body->withInputsOf(ref)) {
                work.push_back(input);
            }
        }
    }
    const auto capturePorts     = body->closureNodes();
    const auto parameterPorts   = body->normPorts();
    if (capturePorts.size() != right.captures.size() || parameterPorts.size() != tangents.size()) {
        why = "the rule's right side does not take the values and tangents the rule binds";
        return rt::kInvalidNodeRef;
    }
    // A capture is a port of the right side's graph, which is one of the arguments the rule is
    // called with: the operands, in the order the left side writes them. Only the captures the body
    // actually reads are bound: a right side that names one operand asks for that one only, and the
    // caller may have no value for the other (a transposed loop body's own state).
    const auto read            = portsReadByBody(*body, keep);
    const auto rhsPorts        = instance.rhs->normPorts();
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> bound;
    for (size_t i = 0; i < capturePorts.size(); ++i) {
        const auto at = std::find(rhsPorts.begin(), rhsPorts.end(), right.captures[i]);
        if (at == rhsPorts.end()) {
            why = "a value the rule's right side captures is not one of its parameters";
            return rt::kInvalidNodeRef;
        }
        if (!keep.empty() && !keep[capturePorts[i]]) {
            // Not part of the answer this position asks for: it needs no value and asks for none.
            continue;
        }
        if (!read[capturePorts[i]]) {
            continue;
        }
        const size_t argument = static_cast<size_t>(at - rhsPorts.begin());
        const gc_node_ref_t value = valueOf == nullptr ? rt::kInvalidNodeRef : valueOf(argument);
        if (value == rt::kInvalidNodeRef) {
            why = "the rule's right side reads a value the caller does not carry";
            return rt::kInvalidNodeRef;
        }
        bound[capturePorts[i]] = value;
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
        if (!keep.empty() && !keep[ref]) {
            continue;
        }
        if (node->kind == rt::GCNodeKind::Port) {
            // A port of the body is a value the call passes: bound, never copied, so the copy has
            // no parameters of its own. A port nothing reads is neither bound nor copied.
            const auto at = bound.find(ref);
            if (at == bound.end()) {
                if (!read[ref]) {
                    continue;
                }
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
        // What the rule's right side passes in angle brackets: `reshape<shape>(dx)` states, on the
        // right, the same shape the left side was written with, so the value is a constant of the
        // rule's own graph -- a `with` input like any other, copied the way an operand is. A with
        // input that is not a constant of the body would have to be the matched node's own context
        // argument, and the expansion carries the rule's values, not the node's: that one is still
        // refused by name, below.
        std::vector<gc_node_ref_t> withs;
        for (gc_node_ref_t input : body->withInputsOf(ref)) {
            const auto at = copied.find(input);
            if (at == copied.end()) {
                why = "the rule's right side passes a context argument it does not carry";
                return rt::kInvalidNodeRef;
            }
            withs.push_back(at->second);
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
            if (!withs.empty()) {
                draft.setWithInputs(made, withs);
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
            if (!inputs.empty() && !tangents.empty()) {
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
            if (!withs.empty()) {
                draft.setWithInputs(made, withs);
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
    // carries the same value. A position asks for one component of that value, which the gate walk
    // above has already found.
    gc_node_ref_t out = position == kWholeAnswer ? body->exitNodeRef() : answerRef;
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

gc_node_ref_t expandRuleByName(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction, std::string_view ruleName,
    const std::function<gc_node_ref_t(size_t)> &valueOf,
    const std::vector<gc_node_ref_t> &tangents, size_t position, std::string &why) {
    using camel::core::type::Type;
    const std::vector<Type *> types{Type::Float64()};
    const auto *instance =
        camel::compile::gir::genericRuleInstanceByName(context, ruleName, types, {}, why);
    if (instance == nullptr) {
        return rt::kInvalidNodeRef;
    }
    RuleRightSide right;
    if (!findRuleRightSide(*instance, right, why)) {
        return rt::kInvalidNodeRef;
    }
    // Whether a value is asked for at all is `expandRuleRightSide`'s call, not this function's: it
    // knows which captures the answer reads -- after the position has picked one component -- and a
    // capture the answer does not read is never asked for, so no operand is invented for a slot the
    // answer does not use.
    return expandRuleRightSide(group, transaction, *instance, valueOf, tangents, why, position);
}

} // namespace camel::transforms