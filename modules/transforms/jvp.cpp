// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "jvp.h"

#include "camel/compile/gir/rule_templates.h"
#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::compile::gir::FrozenGraphRead;
using camel::compile::gir::GraphRead;
using camel::compile::gir::NodeFace;
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

/// The tangent of one primitive: the rule `stdlib/rulesets/jvp.cml` files for its operator, applied
/// to the operands' values and their tangents.
///
/// The rule is read from the family's index every time it is asked for (camel/core/forward.h):
/// remove `jvp_div` from the ruleset and the next question has no answer, which is what a transform
/// must see. There is no table here -- a second writing of the four laws in C++ was the thing this
/// kernel started with and the thing that had to go, because two tables that can disagree is worse
/// than one that is missing.
///
/// A rule is a rewrite, and its right side is a function of the tangents. `reinterp` applies it
/// *at the match site*: the one lambda layer is expanded here, so what stands where the operator
/// stood is a direct call to the named rule function -- `mul_tangent(x, w, dx, dw)`, with the
/// operands the rule captured as the first ports and the tangents after them. The alternative --
/// calling the right side and calling what it answers -- is what a rule rewritten as a *runtime*
/// call produces: a closure-valued node and a call that answers with a closure, neither of which
/// belongs in a graph the transform builds at compile time.
gc_node_ref_t buildRuleTangent(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction,
    std::string_view uri, const std::vector<gc_node_ref_t> &values,
    const std::vector<gc_node_ref_t> &tangents, std::string &ruleName, std::string &why) {
    const auto match = camel::core::forwardRuleOf(std::string(uri));
    if (!match.has_value()) {
        why = "the operator has no forward rule bound at all";
        return rt::kInvalidNodeRef;
    }
    if (match->status != camel::parse::ast::RuleLookup::Found) {
        why = match->status == camel::parse::ast::RuleLookup::NoSuchFamily
                  ? "no ruleset module is loaded (import rulesets.jvp)"
                  : "the ruleset holds no rule for this operator and operand count";
        return rt::kInvalidNodeRef;
    }
    ruleName        = match->rule;
    uint64_t ruleId = 0;
    {
        const auto &table = camel::compile::gir::genericRuleTable(context);
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i] != nullptr && table[i]->name == ruleName) {
                ruleId = static_cast<uint64_t>(i) + 1;
                break;
            }
        }
    }
    if (ruleId == 0) {
        why = "the rule is named in the index but its templates are not registered";
        return rt::kInvalidNodeRef;
    }
    const size_t at = camel::compile::gir::genericRuleInstance(
        context, ruleId, std::vector<camel::core::type::Type *>{Type::Float64()}, {});
    const auto *entry = camel::compile::gir::genericRuleEntry(context, ruleId);
    if (entry == nullptr || at >= entry->instances.size()) {
        why = "no instance of the rule at float";
        return rt::kInvalidNodeRef;
    }
    const auto &instance = entry->instances[at];
    if (!instance.made || instance.program == nullptr || instance.rhs == nullptr) {
        why = instance.why.empty() ? "no instance of the rule at float" : instance.why;
        return rt::kInvalidNodeRef;
    }
    return expandRuleRightSide(group, transaction, instance, values, tangents, why);
}

} // namespace

FunctionType *jvpSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    // What a tangent's type is belongs to the value's type, not to this function: `tangentSpaceOf`
    // is the one answer (camel/core/tangent_space.h), and a float, an array of floats and a tuple of
    // them are each their own space. A result with no tangent space -- an `int`, a `string` -- has
    // nothing to differentiate and is refused here, which is the same refusal as before for every
    // type that was refused, now asked once instead of written out per type.
    const camel::core::TangentSpaceAnswer resultSpace =
        camel::core::tangentSpaceOf(function->exitType());
    if (!resultSpace.hasSpace()) {
        return nullptr;
    }
    auto *lifted = TupleType::create(std::vector<Type *>{function->exitType(), resultSpace.space});
    // Every parameter becomes two parameters, the value and its tangent: `jvp(f)(2.0, 1.0)` passes
    // two numbers, so the graph reads two ports. Handing the pair in as one tuple would need a node
    // that takes it apart, and the only such node -- `Dref` -- belongs to graph construction and is
    // refused outright in a runtime graph (src/runtime/graph.cpp).
    camel::core::type::param_vec_t params;
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        Type *parameter = function->normTypeAt(i);
        const camel::core::TangentSpaceAnswer answer = camel::core::tangentSpaceOf(parameter);
        // A parameter with no tangent space -- the `n` of `xs[n]` -- takes no tangent port, and the
        // lift carries on: the same rule as `Tangent<bool> = ()` with a contribution of zero, stated
        // once, here, by the same question the walk asks (`tangentSpaceOf`). The signature layer used
        // to refuse such a function outright; that refusal is gone, because refusing would make a
        // program that differentiates with respect to `xs` stop compiling for the sake of an index it
        // was told to leave alone. Ports, not a `()` slot: an empty slot would be a place a cotangent
        // could be written, and there is nothing to write (design principles 9.9).
        params.emplace_back(parameter, false);
        if (answer.hasSpace()) {
            params.emplace_back(answer.space, false);
        }
    }
    if (params.empty()) {
        return nullptr;
    }
    // The lifted graph does what the original does to the outside world, so it publishes the same
    // effects: an effectful function's lift is an effectful function (otherwise the run time treats
    // its print as an effect nothing depends on and does not execute it).
    return FunctionType::create(camel::core::type::param_vec_t{}, params, lifted)
        ->withResources(function->resources());
}

namespace {

/// What a lift hands on to the lifts it starts: the group the members live in, and the callee graphs
/// already lifted, so that a function called twice is lifted once (key: the original graph's id).
struct LiftState {
    const camel::core::context::context_ptr_t &context;
    const camel::runtime::Program *program;
    camel::runtime::GraphDraftGroup &group;
    std::unordered_map<uint32_t, camel::runtime::GraphId> callees;
};

bool fail(JvpGraph &refusal) {
    refusal = JvpGraph{};
    return false;
}

bool fail(JvpGraph &refusal, const JvpGraph &why) {
    refusal = why;
    return false;
}

bool liftGraph(
    LiftState &state, const GraphRead &function, std::string_view name,
    camel::runtime::GraphId source, camel::runtime::GraphId member, std::ostream &os,
    JvpGraph &refusal);

} // namespace

/// The build itself, for a caller that has already decided which program the lift's graphs go into:
/// the draft group's. `program` is the program the callee graphs of a body are looked up in (the
/// frozen graph's own for a run-time lift, nothing for one of the lift's own), and `group` is where
/// the lifted graphs are written.
JvpGraph buildJvpGraphInGroup(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GraphDraftGroup &group, const GraphRead &function, std::string_view name,
    camel::runtime::GraphId self, std::ostream &os) {
    const GraphId member = group.reserve(
        "jvp_of_" + std::string(name), "jvp_of_" + std::string(name));
    LiftState state{context, program, group};
    JvpGraph refusal;
    if (!liftGraph(state, function, name, self, member, os, refusal)) {
        return refusal;
    }
    // The count the "one graph per callee" rule is read from: the callee graphs this lift made, in
    // the order they were first called. A function called twice is one entry.
    os << "REINTERP jvp callees=" << state.callees.size() << '\n';
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    return JvpGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

/// The same build for a caller that holds a frozen graph: the face is made here and lives for the
/// call. The name and the identity are the caller's because they are the two facts a draft does
/// not have (it is named and numbered when it is encoded).
JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (function == nullptr) {
        return JvpGraph{};
    }
    const FrozenGraphRead face(*function);
    return buildJvpGraph(context, program, face, function->name(), function->id(), os);
}
JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const GraphRead &function, std::string_view name, camel::runtime::GraphId self,
    std::ostream &os) {
    if (program == nullptr) {
        return JvpGraph{};
    }
    FunctionType *signature = jvpSignature(function.funcType());
    if (signature == nullptr) {
        os << "REINTERP jvp function=" << name << " skip=signature\n";
        return JvpGraph{};
    }
    // A function of any number of `float` parameters: every one of them becomes a pair, so the
    // lifted signature is `(v1, d1, ..., vn, dn) => (value, tangent)`. The tangent of the result is
    // the sum of the directions the caller seeded -- one run reads one direction, and a caller that
    // wants `df/dx` and `df/dy` separately runs it twice with (1, 0) and (0, 1).

    camel::runtime::GraphDraftGroup group(context, program);
    return buildJvpGraphInGroup(context, program, group, function, name, self, os);
}
namespace {

bool liftGraph(
    LiftState &state, const GraphRead &function, std::string_view name,
    camel::runtime::GraphId source, camel::runtime::GraphId member, std::ostream &os,
    JvpGraph &refusal) {
    const camel::core::context::context_ptr_t &context = state.context;
    const camel::runtime::Program *program             = state.program;
    camel::runtime::GraphDraftGroup &group             = state.group;
    FunctionType *signature                            = jvpSignature(function.funcType());
    if (signature == nullptr) {
        return fail(refusal);
    }

    // Steps 1 and 2: a new member with the lifted signature, and one port per parameter, the port
    // itself being the lifted pair `(value, tangent)` the caller passes.
    auto *lifted = TupleType::create(std::vector<Type *>{Type::Float64(), Type::Float64()});
    size_t copiedKinds = 0;
    size_t seenKinds   = 0;
    std::vector<std::string> usedRules;
    // The copy of the loop node the walk made, if any.
    gc_node_ref_t liftedLoopNode = camel::runtime::kInvalidNodeRef;
    {
        auto &transaction = group.transaction(member);
        auto &draft       = transaction.edit();
        draft.setFuncType(signature);
        // The graph this one is a lift of: a fact about the graph, not a comment about the call.
        // `split` reads it when it has to carry a call of a lifted graph into a half, so that the
        // value half can call the primal instead of the lift (whose first parameter is the pair).
        // Every member the lift makes gets it, the top-level `jvp_of_f` and each `jvp_of_callee`.
        // The id is the caller's, like the name: a draft is numbered when it is encoded.
        draft.addLiftedFromRef(source);
        // The recompute request is a fact about the *body*, so a lift keeps it: `remat` marks a
        // graph, `jvp` lifts that graph, and the graph `split` cuts is the lift -- if the mark
        // stopped at the primal, the lift would be cut as if nothing had been asked for, which is
        // what the measurement showed (a cubic lifted from a marked graph still carried two
        // residuals, the same as an unmarked one). Same bit, no new field:
        // `kGraphFlagRecomputeBackward` (`camel/runtime/graph.h`) travels from the source graph to
        // the graph that copies its body.
        if (const camel::runtime::GCGraph *primal = program->graph(source);
            primal != nullptr && primal->recomputesOnBackward()) {
            draft.setFlags(static_cast<uint8_t>(draft.flags() | rt::kGraphFlagRecomputeBackward));
        }
        // One pair of ports per parameter of `f`, in its order: the value the caller passes and the
        // seed of that direction. A port node is a node; the graph's *parameters* are the port nodes
        // it lists as its norm ports, and a call is checked against that list ("has 0 norm ports" is
        // how a graph with the nodes but no entries in the list refused the call) -- so the lifted
        // signature's 2n parameters are these 2n ports, in the same order.
        const size_t parameterCount = function.funcType()->normTypesCount();
        std::vector<gc_node_ref_t> valuePorts;
        std::vector<gc_node_ref_t> tangentPorts;
        valuePorts.reserve(parameterCount);
        tangentPorts.reserve(parameterCount);
        for (size_t i = 0; i < parameterCount; ++i) {
            // The port's type is the parameter's own, and the tangent port's is that parameter's
            // tangent space -- `tangentSpaceOf`'s answer, not `float`: a `float[]` parameter's
            // tangent is an array of the same shape, and a rule that consumes it (`index_into`)
            // takes it whole. The element's tangent only appears one layer down, at `xs[i]`.
            camel::core::type::Type *parameterType = function.funcType()->normTypeAt(i);
            const gc_node_ref_t value = draft.addPortNode(
                parameterType, draft.allocateRuntimeSlot(parameterType));
            draft.appendNormPort(value);
            valuePorts.push_back(value);
            // A parameter with no tangent space takes no tangent port at all -- the same question the
            // signature asks, so the two layers cannot disagree about which parameters have a slot.
            const camel::core::TangentSpaceAnswer tangentSpace =
                camel::core::tangentSpaceOf(parameterType);
            if (tangentSpace.hasSpace()) {
                const gc_node_ref_t tangent = draft.addPortNode(
                    tangentSpace.space, draft.allocateRuntimeSlot(tangentSpace.space));
                draft.appendNormPort(tangent);
                tangentPorts.push_back(tangent);
            } else {
                // No port, and a place in this list that says so: the list is indexed by the
                // parameter's position, so it cannot simply be shorter.
                tangentPorts.push_back(rt::kInvalidNodeRef);
            }
        }
        // Which parameter a port of the original body is, by its place in that list.
        std::unordered_map<gc_node_ref_t, size_t> parameterOf;
        const std::vector<gc_node_ref_t> originalPorts = function.normPorts();
        for (size_t i = 0; i < originalPorts.size(); ++i) {
            parameterOf[originalPorts[i]] = i;
        }
        // Step 3, first layer: the body is copied node by node. A port reads the caller's value, a
        // constant is written again with the same value, and a primitive keeps its operator and gets
        // its inputs from the copies. Nothing is differentiated here -- no rule closure is built --
        // so the chain reproduces the original value; the tangent is still the caller's.
        // NodeIds are 0 .. nodeCount() - 1 (include/camel/runtime/graph.h), so the walk starts at 0:
        // starting at 1 skipped the port, and a primitive that read it then read node 0 of the new
        // graph -- the value came out of an input nobody had copied.
        std::vector<gc_node_ref_t> copyOf(function.nodeCount(), rt::kInvalidNodeRef);
        std::vector<bool> copied(function.nodeCount(), false);
        // The value a node hands to a reader: the copy itself, except where the copy is a lifted
        // pair -- a loop's state port -- and the reader wants the value half.
        std::vector<gc_node_ref_t> valueOf(function.nodeCount(), rt::kInvalidNodeRef);
        bool complete = true;
        // Step 3, second layer: a tangent for every node. A constant's tangent is zero, a port's is
        // the tangent parameter the caller passed, and a primitive's is the forward rule the ruleset
        // files for its operator, applied to its operands' values and tangents -- read from the
        // index at this moment, not from a table written here. A primitive with no rule is refused
        // *by name* rather than answered with 0 or 1, because a wrong derivative that looks like a
        // derivative is the expensive kind of wrong.
        std::vector<gc_node_ref_t> tangentOf(function.nodeCount(), rt::kInvalidNodeRef);
        gc_node_ref_t zeroConstant = rt::kInvalidNodeRef;
        // The lifted pair: a value and its tangent, in the one shape the lifted types are written in.
        // The exit of the transformed body and the exit of an arm are both this pair, so it is made
        // in one place.
        const auto liftedPair = [&draft, lifted](gc_node_ref_t half, gc_node_ref_t halfTangent) {
            ::Tuple *emptyTuple = ::Tuple::create(2, mm::autoSpace());
            const gc_node_ref_t source = draft.materializeStaticValue(
                camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), lifted);
            const gc_node_ref_t pair = draft.addFillNode(
                lifted, rt::makeFillPayload(rt::GCFillKind::Tuple, std::vector<size_t>{0, 1}));
            draft.setNormInputs(pair, std::span<const gc_node_ref_t>(&source, 1));
            const std::array<gc_node_ref_t, 2> halves{half, halfTangent};
            draft.setWithInputs(pair, halves);
            return pair;
        };
        // A branch is a property of the *graph*, not of a node: its arms are regions, the value they
        // yield is the Join's, and an arm that yields a `float` yields the pair in the lifted graph
        // -- so the Join's type is the lifted one, and the Join has to exist before the Brch that
        // names it. Regions and the two nodes are made here; the walk copies nodes as it does
        // anywhere else; the wiring (condition, arm exits, back-references) is done after it, from
        // the original graph's own structure. The action *sequence* is the one every writer of a
        // branch uses (src/passes/opt/defun/defunctionalize.cpp); what a branch means for `jvp` is
        // decided here and nowhere else.
        std::vector<uint32_t> liftedRegion(
            function.regionCount() > 0 ? function.regionCount() : 1, 0);
        for (uint32_t r = 1; r < function.regionCount(); ++r) {
            const rt::GCRegion *from = function.region(r);
            if (from == nullptr) {
                continue;
            }
            const uint32_t parent =
                from->parent < liftedRegion.size() ? liftedRegion[from->parent] : 0;
            liftedRegion[r] = draft.addRegion(from->kind, parent, rt::kInvalidNodeRef, from->index);
        }
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> liftedJoin; // the original Join -> its copy
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> joinOfBrch; // a Brch -> the Join it names
        for (gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Brch) {
                continue;
            }
            rt::GCBrchBody brchBody{};
            const auto *body = function.bodyOf(ref, brchBody) ? &brchBody : nullptr;
            if (body == nullptr || body->join == rt::kInvalidNodeRef) {
                continue;
            }
            const NodeFace *join = function.node(body->join);
            if (join == nullptr) {
                continue;
            }
            Type *joinType = join->dataType() == Type::Float64() ? lifted : join->dataType();
            const gc_node_ref_t made = draft.addJoinNode(joinType, rt::kInvalidNodeRef, body->armCount);
            liftedJoin[body->join]   = made;
            joinOfBrch[ref]          = made;
        }
        // A function value is made before the walk, not when the walk reaches it: a call names its
        // callee through the *value*, and the call may be written before the value in node order.
        // Each original callee graph is lifted once (state.callees), which is what makes two calls
        // of one function one graph.
        std::unordered_map<gc_node_ref_t, GraphId> liftedCalleeOf;
        auto liftFuncValue = [&](gc_node_ref_t ref) -> bool {
            const GraphId calleeId = function.calleeIdOf(ref);
            if (!rt::isValid(calleeId)) {
                return false;
            }
            camel::runtime::GCGraph *callee = program->graph(calleeId);
            if (callee == nullptr) {
                return false;
            }
            // The callee is read through the same kind of face as the caller: a frozen graph here,
            // because this build starts from one.
            const FrozenGraphRead calleeFace(*callee);
            const std::string_view calleeName = callee->name();
            GraphId liftedCallee;
            const auto at = state.callees.find(rt::toValue(calleeId));
            if (at == state.callees.end()) {
                liftedCallee = group.reserve(
                    "jvp_of_" + std::string(callee->name()),
                    "jvp_of_" + std::string(callee->name()));
                state.callees.emplace(rt::toValue(calleeId), liftedCallee);
                JvpGraph sub;
                if (!liftGraph(state, calleeFace, calleeName, calleeId, liftedCallee, os, sub)) {
                    refusal = sub;
                    return false;
                }
            } else {
                liftedCallee = at->second;
            }
            FunctionType *calleeSignature = jvpSignature(callee->funcType());
            if (calleeSignature == nullptr) {
                return false;
            }
            // The call reaches the lifted callee through this node, so the callee is a dependency of this
            // graph by the reference table's own rule (a FUNC node names its callee).
            liftedCalleeOf[ref] = liftedCallee;
            draft.addDependency(liftedCallee);
            return true;
        };
        for (gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Func) {
                continue;
            }
            if (!liftFuncValue(ref)) {
                return fail(refusal);
            }
        }
        for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(rt::GCNodeKind::Dref); ++kind) {
            bool seen = false;
            for (camel::runtime::gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
                const NodeFace *node = function.node(ref);
                if (node != nullptr && static_cast<std::uint8_t>(node->kind) == kind) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                ++seenKinds;
            }
        }
        for (camel::runtime::gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr) {
                continue;
            }
            // A node written inside an arm is copied into that arm: the arms stay arms, and an
            // operator only one arm uses is not computed for the arm that was not taken.
            const uint32_t region = function.nodeRegion(ref);
            draft.setInsertRegion(
                region != 0 && region < liftedRegion.size() ? liftedRegion[region] : 0);
            switch (node->kind) {
            case rt::GCNodeKind::Port: {
                // A parameter of the body is one of the caller's pairs: the value is the first port
                // of that pair, the tangent -- the seed of that direction -- is the second. The
                // chain then adds every direction's contribution, so the exit is the derivative in
                // the direction the caller seeded.
                const auto parameter = parameterOf.find(ref);
                if (parameter != parameterOf.end() && parameter->second < valuePorts.size()) {
                    copyOf[ref]    = valuePorts[parameter->second];
                    tangentOf[ref] = tangentPorts[parameter->second]; // invalid when the parameter has no tangent space
                    copied[ref]    = true;
                    ++copiedKinds;
                    break;
                }
                // A port of a loop's body region is a loop variable, and a `float` one is the
                // carried state: the loop carries the pair, so this port is a pair port and the
                // body reads its halves through projections.
                const uint32_t portRegion = function.nodeRegion(ref);
                const rt::GCRegion *portRegionBody =
                    portRegion != 0 ? function.region(portRegion) : nullptr;
                if (portRegionBody != nullptr &&
                    portRegionBody->kind == rt::GCRegionKind::LoopBody) {
                    const bool carried                     = node->dataType() == Type::Float64();
                    camel::core::type::Type *portType = carried ? lifted : node->dataType();
                    copyOf[ref] = draft.addPortNode(portType, draft.allocateRuntimeSlot(portType));
                    if (carried) {
                        const gc_node_ref_t pairOfState = copyOf[ref];
                        const gc_node_ref_t half = draft.addAccsNode(Type::Float64(), 0);
                        draft.setNormInputs(half, std::span<const gc_node_ref_t>(&pairOfState, 1));
                        const gc_node_ref_t halfTangent = draft.addAccsNode(Type::Float64(), 1);
                        draft.setNormInputs(
                            halfTangent, std::span<const gc_node_ref_t>(&pairOfState, 1));
                        valueOf[ref]   = half;
                        tangentOf[ref] = halfTangent;
                    }
                    copied[ref] = true;
                    ++copiedKinds;
                } else {
                    // Any other port -- the round number a loop writes, a with-parameter -- is a
                    // place a value arrives in: copied as it is, with no tangent.
                    copyOf[ref] = draft.addPortNode(
                        node->dataType(), draft.allocateRuntimeSlot(node->dataType()));
                    copied[ref] = true;
                    ++copiedKinds;
                }
                break;
            }
            case rt::GCNodeKind::Data: {
                const std::vector<slot_t> slotValues(
                    function.staticSlots().begin(), function.staticSlots().end());
                const std::span<const slot_t> slots = slotValues;
                const std::ptrdiff_t index = -static_cast<std::ptrdiff_t>(node->dataIndex);
                if (node->dataIndex < 0 && index >= 0 &&
                    static_cast<size_t>(index) < slots.size()) {
                    copyOf[ref] = draft.materializeStaticValue(
                        slots[static_cast<size_t>(index)], node->dataType(),
                        rt::kGCNodeFlagConstant);
                    // A constant does not move: its tangent is the one zero every rule shares. A
                    // constant of a discrete type (a `bool` condition) has no tangent at all.
                    if (node->dataType() == Type::Float64()) {
                        if (zeroConstant == rt::kInvalidNodeRef) {
                            zeroConstant = draft.materializeStaticValue(
                                camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),
                                rt::kGCNodeFlagConstant);
                        }
                        tangentOf[ref] = zeroConstant;
                    }
                    copied[ref]    = true;
                    ++copiedKinds;
                }
                break;
            }
            case rt::GCNodeKind::Join: {
                // Made before the walk (its type is the lifted one). Its inputs are wired after it.
                const auto at = liftedJoin.find(ref);
                if (at == liftedJoin.end()) {
                    complete = false;
                    break;
                }
                copyOf[ref] = at->second;
                copied[ref] = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Brch: {
                // The condition is wired after the walk, because it is a value the walk has not
                // reached yet. What the branch does not change is the condition itself: it is
                // copied as written and read by the Brch, never differentiated -- the choice of arm
                // is not a value the derivative flows through.
                rt::GCBrchBody brchBody{};
            const auto *body = function.bodyOf(ref, brchBody) ? &brchBody : nullptr;
                const auto at    = joinOfBrch.find(ref);
                if (body == nullptr || at == joinOfBrch.end()) {
                    complete = false;
                    break;
                }
                copyOf[ref] = draft.addBrchNode(Type::Int64(), at->second, body->armCount);
                copied[ref] = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Func: {
                // A `Func` node names its callee graph, and it is one of two things: a function
                // *value* (a type of the function code, carried as it is -- nothing differentiates
                // through a value) or the call itself (the type is the callee's result and the norm
                // inputs are the arguments). A call is lifted as a call: the lifted callee takes
                // pairs and answers with one, so the arguments go in as `(value, tangent)` and the
                // readers of the result project the pair.
                const auto at = liftedCalleeOf.find(ref);
                if (at == liftedCalleeOf.end()) {
                    return fail(refusal);
                }
                camel::runtime::GCGraph *callee = program->graph(function.calleeIdOf(ref));
                if (callee == nullptr) {
                    return fail(refusal);
                }
                FunctionType *calleeSignature = jvpSignature(callee->funcType());
                if (calleeSignature == nullptr) {
                    return fail(refusal);
                }
                const bool isValue = node->dataType() != nullptr &&
                                     node->dataType()->code() == camel::core::type::TypeCode::Function;
                std::vector<gc_node_ref_t> arguments;
                for (gc_node_ref_t input : function.normInputsOf(ref)) {
                    if (input >= function.nodeCount() || !copied[input]) {
                        return fail(refusal);
                    }
                    if (isValue) {
                        break;
                    }
                    const NodeFace *in = function.node(input);
                    if (in != nullptr && in->dataType() == Type::Float64()) {
                        if (tangentOf[input] == rt::kInvalidNodeRef) {
                            return fail(refusal);
                        }
                        const gc_node_ref_t half =
                            valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input];
                        // The lifted callee takes the value and its tangent as two ordinary
                        // arguments (the signature is (float, float) => ...), not as one tuple.
                        arguments.push_back(half);
                        arguments.push_back(tangentOf[input]);
                    } else {
                        arguments.push_back(copyOf[input]);
                    }
                }
                camel::core::type::Type *valueType =
                    isValue ? static_cast<camel::core::type::Type *>(calleeSignature) : lifted;
                const gc_node_ref_t made = draft.addFuncNode(
                    at->second, valueType, 0, draft.allocateRuntimeSlot(valueType), 0);
                if (!isValue) {
                    if (!arguments.empty()) {
                        draft.setNormInputs(made, arguments);
                    }
                    std::vector<gc_node_ref_t> carries;
                    for (gc_node_ref_t input : function.withInputsOf(ref)) {
                        if (input >= function.nodeCount() || !copied[input]) {
                            return fail(refusal);
                        }
                        carries.push_back(copyOf[input]);
                    }
                    if (!carries.empty()) {
                        draft.setWithInputs(made, carries);
                    }
                }
                copyOf[ref] = made;
                if (!isValue) {
                    const gc_node_ref_t half = draft.addAccsNode(Type::Float64(), 0);
                    draft.setNormInputs(half, std::span<const gc_node_ref_t>(&made, 1));
                    const gc_node_ref_t halfTangent = draft.addAccsNode(Type::Float64(), 1);
                    draft.setNormInputs(halfTangent, std::span<const gc_node_ref_t>(&made, 1));
                    valueOf[ref]   = half;
                    tangentOf[ref] = halfTangent;
                }
                copied[ref] = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Call: {
                // A call passes the lifted callee its arguments as pairs and answers with a pair.
                // The first norm input is the function value, the rest are the arguments; a `float`
                // argument goes in as `(value, tangent)`.
                std::vector<gc_node_ref_t> args;
                bool isCallee = true;
                for (gc_node_ref_t input : function.normInputsOf(ref)) {
                    if (input >= function.nodeCount() || !copied[input]) {
                        return fail(refusal);
                    }
                    if (isCallee) {
                        args.push_back(copyOf[input]);
                        isCallee = false;
                        continue;
                    }
                    const NodeFace *in = function.node(input);
                    if (in != nullptr && in->dataType() == Type::Float64()) {
                        if (tangentOf[input] == rt::kInvalidNodeRef) {
                            return fail(refusal);
                        }
                        const gc_node_ref_t half =
                            valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input];
                        // Two ordinary arguments, not one tuple: the lifted signature takes
                        // `(value, tangent)` as two floats.
                        args.push_back(half);
                        args.push_back(tangentOf[input]);
                    } else {
                        args.push_back(copyOf[input]);
                    }
                }
                if (args.empty()) {
                    return fail(refusal);
                }
                const gc_node_ref_t made = draft.addCallNode(lifted, rt::GCCallBody{});
                draft.setNormInputs(made, args);
                std::vector<gc_node_ref_t> captures;
                for (gc_node_ref_t input : function.withInputsOf(ref)) {
                    if (input >= function.nodeCount() || !copied[input]) {
                        return fail(refusal);
                    }
                    captures.push_back(copyOf[input]);
                }
                if (!captures.empty()) {
                    draft.setWithInputs(made, captures);
                }
                copyOf[ref] = made;
                // The call's value is the pair: a reader wants the value half, a rule the tangent.
                const gc_node_ref_t half = draft.addAccsNode(Type::Float64(), 0);
                draft.setNormInputs(half, std::span<const gc_node_ref_t>(&made, 1));
                const gc_node_ref_t halfTangent = draft.addAccsNode(Type::Float64(), 1);
                draft.setNormInputs(halfTangent, std::span<const gc_node_ref_t>(&made, 1));
                valueOf[ref]   = half;
                tangentOf[ref] = halfTangent;
                copied[ref]    = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Loop: {
                // A loop is a region of this graph: the rounds are the region's and the LOOP
                // node's value is the state. Lifting it is lifting the state into a pair -- the
                // node's type is the pair, the body's state port is a pair port (whose halves the
                // body reads through projections), and the region's result is the pair of the
                // body's new value and its tangent. The inputs and the region are wired after the
                // walk. The node names no graph: the rounds are the region (design principles 3.10).
                rt::GCLoopBody body{};
                function.bodyOf(ref, body);
                rt::GCLoopBody liftedBody = body;
                copyOf[ref]               = draft.addLoopNode(liftedBody, lifted);
                liftedLoopNode            = copyOf[ref];
                copied[ref]               = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Oper: {
                camel::runtime::GCOperBody operBody{};
            const auto *body = function.bodyOf(ref, operBody) ? &operBody : nullptr;
                const gc_node_ref_t made =
                    draft.addOperNode(node->dataType(), nullptr, body->uri());
                std::vector<gc_node_ref_t> originals;
                std::vector<gc_node_ref_t> inputs;
                for (gc_node_ref_t input : function.normInputsOf(ref)) {
                    // A primitive whose input was not copied cannot be reproduced: the copy would
                    // name a node that is not there, and the transform would answer with a graph
                    // that computes something else. The build says so and gives nothing.
                    if (input >= function.nodeCount() || !copied[input]) {
                        complete = false;
                        break;
                    }
                    originals.push_back(input);
                    inputs.push_back(valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input]);
                }
                copyOf[ref] = made;
                copied[ref] = true;
                ++copiedKinds;
                if (!complete) {
                    break;
                }
                if (!inputs.empty()) {
                    draft.setNormInputs(made, inputs);
                }
                // The `sync` block's chain, copied edge for edge: a statement is executed because it
                // sits on that chain, and an effect whose result nobody reads is dropped at run time
                // without it. The flag says the node is one of the block's statements; the waits are
                // the statements written before it.
                {
                    std::vector<gc_node_ref_t> waits;
                    for (gc_node_ref_t edge : function.syncInputsOf(ref)) {
                        if (edge < function.nodeCount() && copied[edge]) {
                            waits.push_back(copyOf[edge]);
                        }
                    }
                    if (!waits.empty()) {
                        draft.setSyncInputs(made, waits);
                    }
                    // The flag is the original's own: a statement the block wrote, whether or not
                    // anything was written before it (the first statement has no waits and is still
                    // a statement).
                    if (node->hasFlag(rt::kGCNodeFlagSyncStatement)) {
                        draft.setNodeRuntimeFlags(
                            made, static_cast<uint8_t>(rt::kGCNodeFlagSyncStatement));
                    }
                }
                if (!camel::core::tangentSpaceOf(node->dataType()).hasSpace()) {
                    // A discrete value -- a comparison's `bool`, a count's `int` -- has no tangent:
                    // it is copied so the nodes that read it have their operand, and no rule is
                    // asked to produce a derivative for it (design principles 9.9: a discrete
                    // value's derivative is zero by definition, and a zero nobody reads is not
                    // worth a node). Whether a value has a tangent at all is `tangentSpaceOf`'s
                    // answer, not a comparison with `float`: an array of floats is its own tangent
                    // space, and it is walked like a float -- by the rule filed under its operator.
                    break;
                }
                const std::string_view uri = body->uri();
                // An operator with an external effect (a print, the clock, the profiler) is
                // executed as written, and its derivative is zero: asking the rule table for a
                // forward rule would ask it to differentiate a write to the outside world, and the
                // honest answer -- "there is no such rule" -- would stop a program whose effect
                // the lift has no business touching. The copy keeps the operator and its inputs, so
                // the effect runs the same number of times it ran before.
                if (camel::core::OperatorTable::instance().hasExternalEffect(uri)) {
                    if (node->dataType() == Type::Float64()) {
                        if (zeroConstant == rt::kInvalidNodeRef) {
                            zeroConstant = draft.materializeStaticValue(
                                camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),
                                rt::kGCNodeFlagConstant);
                        }
                        tangentOf[ref] = zeroConstant;
                    }
                    break;
                }
                std::vector<gc_node_ref_t> tangents;
                for (gc_node_ref_t original : originals) {
                    // An operand with no tangent space -- the `i` of `xs[i]`, a branch's condition --
                    // contributes no slot: the rule is handed the tangents of the operands that have
                    // one, in their own order (design principles 9.9, and the same "no slot" the
                    // `Tangent<bool> = ()` reading uses -- a slot that does not exist, not a zero
                    // that does).
                    const NodeFace *input = function.node(original);
                    if (input != nullptr && !camel::core::tangentSpaceOf(input->dataType()).hasSpace()) {
                        continue;
                    }
                    if (tangentOf[original] == rt::kInvalidNodeRef) {
                        complete = false;
                        break;
                    }
                    tangents.push_back(tangentOf[original]);
                }
                if (!complete) {
                    break;
                }
                std::string ruleName;
                std::string ruleWhy;
                tangentOf[ref] =
                    buildRuleTangent(
                        *context, state.group, transaction, uri, inputs, tangents,
                        ruleName, ruleWhy);
                if (tangentOf[ref] == rt::kInvalidNodeRef) {
                    // Named, not guessed: the transform has no derivative for this operator, and
                    // the refusal carries the place the primitive was written -- the node's origin,
                    // which the kernel turns into a fault at that `file:line` -- so the error names
                    // the operator in `f`, not the call that asked for `jvp`.
                    os << "REINTERP jvp function=" << name << " skip=rule uri=" << uri
                       << " reason=" << ruleWhy << '\n';
                    JvpGraph refused;
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(
                        function.nodeOrigin(ref));
                    refused.refusedUri = std::string(uri);
                    refused.refusedWhy = ruleWhy;
                    return fail(refusal, refused);
                }
                if (std::find(usedRules.begin(), usedRules.end(), ruleName) == usedRules.end()) {
                    usedRules.push_back(ruleName);
                }
                break;
            }
            default:
                break;
            }
        }
        if (!complete) {
            os << "REINTERP jvp function=" << name << " skip=body\n";
            return fail(refusal);
        }
        draft.setInsertRegion(0);
        // The wiring the walk deferred. Each arm yields the lifted pair of its value and its
        // tangent, the Join names the Brch and those pairs, and the Brch names its condition and
        // its Join. Which arm is which, and what each arm yields, is read from the original graph;
        // nothing about branches is decided a second time here.
        for (gc_node_ref_t ref = 0; complete && ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Brch) {
                continue;
            }
            rt::GCBrchBody brchBody{};
            const auto *body     = function.bodyOf(ref, brchBody) ? &brchBody : nullptr;
            const gc_node_ref_t brch = copyOf[ref];
            if (body == nullptr || brch == rt::kInvalidNodeRef) {
                complete = false;
                break;
            }
            std::vector<gc_node_ref_t> condition;
            for (gc_node_ref_t input : function.normInputsOf(ref)) {
                if (input >= function.nodeCount() || !copied[input]) {
                    complete = false;
                    break;
                }
                condition.push_back(copyOf[input]);
            }
            if (!complete) {
                break;
            }
            if (!condition.empty()) {
                draft.setNormInputs(brch, condition);
            }
            std::vector<gc_node_ref_t> armResults;
            for (rt::gc_cnt_t k = 0; k < body->armCount; ++k) {
                const rt::GCRegion *arm = nullptr;
                uint32_t armRegion      = 0;
                for (uint32_t r = 1; r < function.regionCount(); ++r) {
                    const rt::GCRegion *candidate = function.region(r);
                    if (candidate != nullptr && candidate->kind == rt::GCRegionKind::Arm &&
                        candidate->owner == ref && candidate->index == static_cast<uint32_t>(k)) {
                        arm       = candidate;
                        armRegion = r;
                        break;
                    }
                }
                if (arm == nullptr || arm->result == rt::kInvalidNodeRef ||
                    !copied[arm->result] || tangentOf[arm->result] == rt::kInvalidNodeRef) {
                    complete = false;
                    break;
                }
                draft.setInsertRegion(liftedRegion[armRegion]);
                const gc_node_ref_t pair = liftedPair(copyOf[arm->result], tangentOf[arm->result]);
                draft.setRegionOwner(liftedRegion[armRegion], brch, static_cast<uint32_t>(k));
                draft.setRegionResult(liftedRegion[armRegion], pair);
                draft.setInsertRegion(0);
                armResults.push_back(pair);
            }
            if (!complete) {
                break;
            }
            const gc_node_ref_t join = joinOfBrch.at(ref);
            const rt::DraftNodeHeader *header = draft.header(join);
            rt::DraftNodeInit init;
            init.dataIndex    = header->dataIndex;
            init.dataType     = header->dataType;
            init.kind         = rt::GCNodeKind::Join;
            init.runtimeFlags = header->runtimeFlags;
            init.origin       = header->origin;
            init.permissions  = header->permissions;
            init.attrs        = header->attrs;
            init.region       = header->region;
            init.name         = header->name;
            const rt::GCJoinBody joinBody{.brch = brch, .armCount = body->armCount};
            init.payload = std::span<const std::byte>(
                reinterpret_cast<const std::byte *>(&joinBody), sizeof(joinBody));
            init.normInputs = std::span<const gc_node_ref_t>(&brch, 1);
            init.withInputs = armResults;
            draft.rewriteNode(join, init);
        }
        if (!complete) {
            os << "REINTERP jvp function=" << name << " skip=branch\n";
            return fail(refusal);
        }
        // A loop's wiring: its state input becomes a pair (the initial state and its zero tangent),
        // its body region keeps its ports -- the state port now a pair -- and its result becomes the
        // pair the body computes. The loop's value is then a lifted value like any other, so a
        // reader of it needs nothing special, including the body's exit.
        for (gc_node_ref_t ref = 0; complete && ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Loop) {
                continue;
            }
            const gc_node_ref_t loopCopy = copyOf[ref];
            if (loopCopy == rt::kInvalidNodeRef) {
                complete = false;
                break;
            }
            // The body region comes first: its result is the pair the state becomes, and the loop
            // node's *first with-input* is that same result (LoopView::bodyResult reads it there),
            // so it has to be the pair and not the copy of the old exit.
            gc_node_ref_t newState = rt::kInvalidNodeRef;
            for (uint32_t r = 1; r < function.regionCount(); ++r) {
                const rt::GCRegion *part = function.region(r);
                if (part == nullptr || part->owner != ref ||
                    part->kind != rt::GCRegionKind::LoopBody) {
                    continue;
                }
                draft.setRegionOwner(liftedRegion[r], loopCopy, part->index);
                std::vector<gc_node_ref_t> ports;
                for (gc_node_ref_t port : function.regionPorts(r)) {
                    if (port >= function.nodeCount() || !copied[port]) {
                        complete = false;
                        break;
                    }
                    ports.push_back(copyOf[port]);
                }
                if (!complete) {
                    break;
                }
                if (!ports.empty()) {
                    draft.setRegionPorts(liftedRegion[r], ports);
                }
                const gc_node_ref_t exit = part->result;
                if (exit == rt::kInvalidNodeRef || !copied[exit] ||
                    tangentOf[exit] == rt::kInvalidNodeRef) {
                    complete = false;
                    break;
                }
                draft.setInsertRegion(liftedRegion[r]);
                newState = liftedPair(copyOf[exit], tangentOf[exit]);
                draft.setRegionResult(liftedRegion[r], newState);
                draft.setInsertRegion(0);
            }
            if (!complete || newState == rt::kInvalidNodeRef) {
                complete = false;
                break;
            }
            std::vector<gc_node_ref_t> loopInputs;
            for (gc_node_ref_t input : function.normInputsOf(ref)) {
                if (input >= function.nodeCount() || !copied[input]) {
                    complete = false;
                    break;
                }
                const NodeFace *in = function.node(input);
                const gc_node_ref_t copy         = copyOf[input];
                if (in != nullptr && in->dataType() == Type::Float64()) {
                    if (tangentOf[input] == rt::kInvalidNodeRef) {
                        complete = false;
                        break;
                    }
                    loopInputs.push_back(liftedPair(copy, tangentOf[input]));
                } else {
                    loopInputs.push_back(copy);
                }
            }
            if (!complete) {
                break;
            }
            if (!loopInputs.empty()) {
                draft.setNormInputs(loopCopy, loopInputs);
            }
            // The first with-input is the body's result -- the new state; the rest (a round index,
            // what the body reads from outside) are copied as they are.
            std::vector<gc_node_ref_t> loopWith;
            loopWith.push_back(newState);
            const auto loopWithOriginals = function.withInputsOf(ref);
            for (size_t i = 1; i < loopWithOriginals.size(); ++i) {
                const gc_node_ref_t input = loopWithOriginals[i];
                if (input >= function.nodeCount() || !copied[input]) {
                    complete = false;
                    break;
                }
                loopWith.push_back(copyOf[input]);
            }
            if (!complete) {
                break;
            }
            draft.setWithInputs(loopCopy, loopWith);
        }
        if (!complete) {
            os << "REINTERP jvp function=" << name << " skip=loop\n";
            return fail(refusal);
        }
        // The value the copied chain produces: the node the original graph's exit names. The exit
        // node *is* the value-producing node of a compiled graph (its inputs are that node's
        // operands), so the value is the copy of the exit itself -- and a Gate only forwards, so it
        // is followed to what it carries.
        gc_node_ref_t oldValue = function.exitNodeRef();
        while (oldValue != rt::kInvalidNodeRef) {
            const NodeFace *node = function.node(oldValue);
            if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
                break;
            }
            const auto norm = function.normInputsOf(oldValue);
            if (!norm.empty()) {
                oldValue = norm.back();
                continue;
            }
            const auto with = function.withInputsOf(oldValue);
            if (!with.empty()) {
                oldValue = with.back();
                continue;
            }
            oldValue = rt::kInvalidNodeRef;
        }
        if (oldValue >= function.nodeCount() || !copied[oldValue]) {
            os << "REINTERP jvp function=" << name << " skip=value\n";
            return fail(refusal);
        }
        const gc_node_ref_t chainValue = copyOf[oldValue];
        const gc_node_ref_t chainTangent = tangentOf[oldValue];
        // A body that ends at a branch already yields the pair -- the Join's type is the lifted one
        // -- so the exit is that value itself. A straight-line body yields a value the rules gave a
        // tangent for, and the exit is the pair built from the two.
        const rt::DraftNodeHeader *chain = draft.header(chainValue);
        const bool alreadyLifted = chain != nullptr && chain->dataType == lifted;
        if (chainTangent == rt::kInvalidNodeRef && !alreadyLifted) {
            os << "REINTERP jvp function=" << name << " skip=tangent\n";
            return fail(refusal);
        }
        // The exit is the lifted result: the value the copied chain computes and the tangent the
        // rules computed for it, which is the derivative of the chain at the values the caller
        // passed. Nothing is read back from the original graph: both halves are this graph's.
        const gc_node_ref_t out = alreadyLifted ? chainValue : liftedPair(chainValue, chainTangent);
        // The `sync` block's own ordering: in the original graph the exit passes through a GATE that
        // waits on the block's statements -- a print among them -- and that wait is the only thing
        // that keeps an effect whose result nobody reads from being dropped. The lifted exit is a
        // gate over the pair with the same waits, so the effect runs once, at the same point.
        gc_node_ref_t exit = out;
        const gc_node_ref_t originalExit = function.exitNodeRef();
        if (originalExit < function.nodeCount() && function.node(originalExit) != nullptr &&
            function.node(originalExit)->kind == rt::GCNodeKind::Gate) {
            const gc_node_ref_t gate = draft.addGateNode(lifted, draft.allocateRuntimeSlot(lifted));
            draft.setNormInputs(gate, std::span<const gc_node_ref_t>(&out, 1));
            std::vector<gc_node_ref_t> waits;
            for (gc_node_ref_t edge : function.syncInputsOf(originalExit)) {
                if (edge < function.nodeCount() && copied[edge]) {
                    waits.push_back(copyOf[edge]);
                }
            }
            if (!waits.empty()) {
                draft.setSyncInputs(gate, waits);
            }
            if (function.node(originalExit)->hasFlag(rt::kGCNodeFlagSyncStatement)) {
                draft.setNodeRuntimeFlags(gate, static_cast<uint8_t>(rt::kGCNodeFlagSyncStatement));
            }
            exit = gate;
        }
        draft.setExitNode(exit);
        draft.setOutputNode(exit);
        draft.setReturnNode(exit, rt::GCReturnKind::Self);
        // The reading: one line per graph the lift made -- the function asked for and every callee it
        // had to lift -- so "a function called twice is lifted once" is countable, not claimed.
        std::string rules;
        for (const std::string &rule : usedRules) {
            rules += (rules.empty() ? "" : ",") + rule;
        }
        os << "REINTERP jvp function=" << name << " graph=" << rt::toValue(member)
           // The graph the lift was asked about. A run-time lift knows it -- it is the callee graph
           // the call named -- and prints its id there. A build-time lift does not and cannot: the
           // callee's runtime id is handed out when the program is encoded, which is after this
           // build, and a number here would read as the id of some graph. So the reading is a word
           // for "not assigned yet" rather than a number that looks like an answer.
           << " liftedFrom="
           << (rt::isValid(source) ? std::to_string(rt::toValue(source)) : std::string("unassigned"))
           << " type=" << signature->toString() << " nodes=" << function.nodeCount()
           << " kinds=" << copiedKinds << '/' << seenKinds
           << " exit=Tuple{chain,tangent} tangent=computed source=ruleset rules="
           << (rules.empty() ? "-" : rules) << '\n';
        // The reading the next step needs: the graph the lift has just finished, in one line.
        // `graph` is the member it was reserved as, `nodes` and `regions` are what the draft holds
        // at this moment -- before the group is encoded, which only numbers what is here -- and
        // `exit` is the node the graph answers with, the pair or the gate over it. Nothing is read
        // back and nothing is decided here: the line is printed and the build goes on exactly as it
        // did, so moving this build to compile time can be checked by the same numbers.
        os << "REINTERP jvp built graph=" << rt::toValue(member) << " nodes=" << draft.nodeCount()
           << " exit=" << exit << " regions=" << draft.regionCount() << " function=" << name
           << '\n';
    }
    return true;
}

} // namespace

} // namespace camel::transforms
