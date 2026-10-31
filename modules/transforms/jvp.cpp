// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "jvp.h"

#include "camel/compile/gir/rule_templates.h"
#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
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
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace {

/// The tangent of one primitive: the rule `stdlib/rulesets/jvp.cml` files for its operator, applied
/// to the operands' values and their tangents.
///
/// The rule is read from the family's index every time it is asked for (camel/core/forward.h):
/// remove `jvp_div` from the ruleset and the next question has no answer, which is what a transform
/// must see. There is no table here -- a second writing of the four laws in C++ was the thing this
/// kernel started with and the thing that had to go, because two tables that can disagree is worse
/// than one that is missing.
///
/// A rule's right side as an instance is curried, not closed over: `(x, w) => ((dx, dw) =>
/// Tangent<T>)`. So the graph calls it once with the operands the primitive is about, and calls what
/// it answers with the tangents. Nothing is captured and no closure is built -- the operands are the
/// rule's own parameters.
gc_node_ref_t buildRuleTangent(
    camel::core::context::Context &context, camel::runtime::GraphTransaction &transaction,
    std::string_view uri, const std::vector<gc_node_ref_t> &values,
    const std::vector<gc_node_ref_t> &tangents, std::string &ruleName, std::string &why) {
    // The graph is written through the transaction that holds it, never through a reference taken
    // beside it: `GraphTransaction::borrow` exists for a caller that has a draft whose owner is not a
    // transaction at hand, and the ledger of direct connections to the graph store counts the ones
    // that skip it.
    auto &draft = transaction.edit();
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
    // The right side as an instance is curried, not closed over: `(x, w) => ((dx, dw) => Tangent<T>)`.
    // It takes the operands it is about and answers with the function that takes the tangents, so the
    // graph asks it once with the operands and calls what comes back with the tangents. No closure is
    // built and nothing is captured: the operands become arguments, which is the whole shape.
    auto *rhsType = instance.rhs->funcType();
    if (rhsType == nullptr || !rhsType->hasExitType() || rhsType->exitType() == nullptr ||
        rhsType->exitType()->code() != camel::core::type::TypeCode::Function) {
        why = "the rule's right side does not answer with a function of the tangents";
        return rt::kInvalidNodeRef;
    }
    auto *partialType = tt::as_ptr<FunctionType>(rhsType->exitType());
    // A rule's right side captures nothing: the operands it is about are its own parameters, so its
    // closure is the empty tuple (`Function::create` asks for one rather than for none).
    const camel::core::type::TupleType *closureType = instance.rhs->closureType();
    if (closureType == nullptr) {
        closureType = TupleType::create(std::vector<Type *>{});
    }
    auto *function = ::Function::create(
        instance.program, instance.rhs->id(), closureType, mm::autoSpace());
    if (function == nullptr) {
        why = "the rule's right side is not a graph a function value can name";
        return rt::kInvalidNodeRef;
    }
    const gc_node_ref_t rule = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<::Function *>(function), rhsType);
    const gc_node_ref_t partial = draft.addCallNode(partialType);
    draft.setNormInputs(partial, values);
    draft.setWithInputs(partial, std::span<const gc_node_ref_t>(&rule, 1));
    const gc_node_ref_t tangent = draft.addCallNode(partialType->exitType());
    draft.setNormInputs(tangent, tangents);
    draft.setWithInputs(tangent, std::span<const gc_node_ref_t>(&partial, 1));
    return tangent;
}

} // namespace

FunctionType *jvpSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    Type *result = function->exitType();
    if (result != Type::Float64()) {
        return nullptr;
    }
    auto *lifted = TupleType::create(std::vector<Type *>{Type::Float64(), Type::Float64()});
    // Every parameter becomes two parameters, the value and its tangent: `jvp(f)(2.0, 1.0)` passes
    // two numbers, so the graph reads two ports. Handing the pair in as one tuple would need a node
    // that takes it apart, and the only such node -- `Dref` -- belongs to graph construction and is
    // refused outright in a runtime graph (src/runtime/graph.cpp).
    camel::core::type::param_vec_t params;
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        if (function->normTypeAt(i) != Type::Float64()) {
            return nullptr;
        }
        params.emplace_back(Type::Float64(), false);
        params.emplace_back(Type::Float64(), false);
    }
    if (params.empty()) {
        return nullptr;
    }
    return FunctionType::create(camel::core::type::param_vec_t{}, params, lifted);
}

JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return JvpGraph{};
    }
    FunctionType *signature = jvpSignature(function->funcType());
    if (signature == nullptr) {
        os << "REINTERP jvp function=" << function->name() << " skip=signature\n";
        return JvpGraph{};
    }
    // One parameter, for now: with two the exit would need a way to combine two lifted pairs into
    // the one lifted result, and that is a node in the body -- step 3's business, not this one's.
    if (function->funcType()->normTypesCount() != 1) {
        os << "REINTERP jvp function=" << function->name()
           << " skip=params " << function->funcType()->normTypesCount() << "\n";
        return JvpGraph{};
    }

    // Steps 1 and 2: a new member with the lifted signature, and one port per parameter, the port
    // itself being the lifted pair `(value, tangent)` the caller passes.
    auto *lifted = TupleType::create(std::vector<Type *>{Type::Float64(), Type::Float64()});
    size_t copiedKinds = 0;
    size_t seenKinds   = 0;
    std::vector<std::string> usedRules;
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "jvp_of_" + std::string(function->name()), "jvp_of_" + std::string(function->name()));
    {
<<<<<<< HEAD
        auto &draft = group.transaction(member).edit();
||||||| 439e2a46f
        auto &draft = group.draft(member);
=======
        auto &draft = group.draft(member);
        // The writings a caller builds from a draft alone still go through a transaction
        // (GraphTransaction::borrow): the draft's owner is the group, and a rule's tangent is built
        // by a piece of code that is handed the transaction rather than the graph.
        auto transaction = camel::runtime::GraphTransaction::borrow(draft);
>>>>>>> lane-recheck
        draft.setFuncType(signature);
        const gc_node_ref_t value =
            draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));
        const gc_node_ref_t tangent =
            draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));
        // A port node is a node; the graph's *parameters* are the port nodes it lists as its norm
        // ports, and a call is checked against that list ("has 0 norm ports" is how a graph with
        // the two nodes but no entries in the list refused the call). The lifted signature has two
        // parameters -- the value and its tangent -- and these are the two the caller passes.
        draft.appendNormPort(value);
        draft.appendNormPort(tangent);
        // Step 3, first layer: the body is copied node by node. A port reads the caller's value, a
        // constant is written again with the same value, and a primitive keeps its operator and gets
        // its inputs from the copies. Nothing is differentiated here -- no rule closure is built --
        // so the chain reproduces the original value; the tangent is still the caller's.
        // NodeIds are 0 .. nodeCount() - 1 (include/camel/runtime/graph.h), so the walk starts at 0:
        // starting at 1 skipped the port, and a primitive that read it then read node 0 of the new
        // graph -- the value came out of an input nobody had copied.
        std::vector<gc_node_ref_t> copyOf(function->nodeCount(), rt::kInvalidNodeRef);
        std::vector<bool> copied(function->nodeCount(), false);
        bool complete = true;
        // Step 3, second layer: a tangent for every node. A constant's tangent is zero, a port's is
        // the tangent parameter the caller passed, and a primitive's is the forward rule the ruleset
        // files for its operator, applied to its operands' values and tangents -- read from the
        // index at this moment, not from a table written here. A primitive with no rule is refused
        // *by name* rather than answered with 0 or 1, because a wrong derivative that looks like a
        // derivative is the expensive kind of wrong.
        std::vector<gc_node_ref_t> tangentOf(function->nodeCount(), rt::kInvalidNodeRef);
        gc_node_ref_t zeroConstant = rt::kInvalidNodeRef;
        for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(rt::GCNodeKind::Dref); ++kind) {
            bool seen = false;
            for (camel::runtime::gc_node_ref_t ref = 0; ref < function->nodeCount(); ++ref) {
                const camel::runtime::GCNode *node = function->node(ref);
                if (node != nullptr && static_cast<std::uint8_t>(node->kind) == kind) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                ++seenKinds;
            }
        }
        for (camel::runtime::gc_node_ref_t ref = 0; ref < function->nodeCount(); ++ref) {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                continue;
            }
            switch (node->kind) {
            case rt::GCNodeKind::Port:
                // The lifted graph has one ordinary parameter, so the one port the original body
                // reads is the value the caller passes. Its tangent is the second parameter.
                if (!function->normPorts().empty() && function->normPorts().front() == ref) {
                    copyOf[ref]    = value;
                    tangentOf[ref] = tangent;
                    copied[ref]    = true;
                    ++copiedKinds;
                }
                break;
            case rt::GCNodeKind::Data: {
                const std::span<const slot_t> slots =
                    function->staticSlots();
                const std::ptrdiff_t index = -static_cast<std::ptrdiff_t>(node->dataIndex);
                if (node->dataIndex < 0 && index >= 0 &&
                    static_cast<size_t>(index) < slots.size()) {
                    copyOf[ref] = draft.materializeStaticValue(
                        slots[static_cast<size_t>(index)], node->dataType(),
                        rt::kGCNodeFlagConstant);
                    // A constant does not move: its tangent is the one zero every rule shares.
                    if (zeroConstant == rt::kInvalidNodeRef) {
                        zeroConstant = draft.materializeStaticValue(
                            camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),
                            rt::kGCNodeFlagConstant);
                    }
                    tangentOf[ref] = zeroConstant;
                    copied[ref]    = true;
                    ++copiedKinds;
                }
                break;
            }
            case rt::GCNodeKind::Oper: {
                const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const gc_node_ref_t made =
                    draft.addOperNode(node->dataType(), nullptr, body->uri());
                std::vector<gc_node_ref_t> originals;
                std::vector<gc_node_ref_t> inputs;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    // A primitive whose input was not copied cannot be reproduced: the copy would
                    // name a node that is not there, and the transform would answer with a graph
                    // that computes something else. The build says so and gives nothing.
                    if (input >= function->nodeCount() || !copied[input]) {
                        complete = false;
                        break;
                    }
                    originals.push_back(input);
                    inputs.push_back(copyOf[input]);
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
                const std::string_view uri = body->uri();
                std::vector<gc_node_ref_t> tangents;
                for (gc_node_ref_t original : originals) {
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
                    buildRuleTangent(*context, *transaction, uri, inputs, tangents, ruleName, ruleWhy);
                if (tangentOf[ref] == rt::kInvalidNodeRef) {
                    // Named, not guessed: the transform has no derivative for this operator, and
                    // the refusal carries the place the primitive was written -- the node's origin,
                    // which the kernel turns into a fault at that `file:line` -- so the error names
                    // the operator in `f`, not the call that asked for `jvp`.
                    os << "REINTERP jvp function=" << function->name() << " skip=rule uri=" << uri
                       << " reason=" << ruleWhy << '\n';
                    JvpGraph refused;
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(
                        function->nodeOrigin(ref));
                    refused.refusedUri = std::string(uri);
                    refused.refusedWhy = ruleWhy;
                    return refused;
                }
                if (std::find(usedRules.begin(), usedRules.end(), ruleName) == usedRules.end()) {
                    usedRules.push_back(ruleName);
                }
                break;
            }
            default:
                break;
            }
            if (!complete) {
                break;
            }
        }
        if (!complete) {
            os << "REINTERP jvp function=" << function->name() << " skip=body\n";
            return JvpGraph{};
        }
        // The value the copied chain produces: the node the original graph's exit names. The exit
        // node *is* the value-producing node of a compiled graph (its inputs are that node's
        // operands), so the value is the copy of the exit itself -- and a Gate only forwards, so it
        // is followed to what it carries.
        gc_node_ref_t oldValue = function->exitNodeRef();
        while (oldValue != rt::kInvalidNodeRef) {
            const camel::runtime::GCNode *node = function->node(oldValue);
            if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
                break;
            }
            const auto norm = function->normInputsOf(oldValue);
            if (!norm.empty()) {
                oldValue = norm.back();
                continue;
            }
            const auto with = function->withInputsOf(oldValue);
            if (!with.empty()) {
                oldValue = with.back();
                continue;
            }
            oldValue = rt::kInvalidNodeRef;
        }
        if (oldValue >= function->nodeCount() || !copied[oldValue]) {
            os << "REINTERP jvp function=" << function->name() << " skip=value\n";
            return JvpGraph{};
        }
        const gc_node_ref_t chainValue = copyOf[oldValue];
        const gc_node_ref_t chainTangent = tangentOf[oldValue];
        if (chainTangent == rt::kInvalidNodeRef) {
            os << "REINTERP jvp function=" << function->name() << " skip=tangent\n";
            return JvpGraph{};
        }
        // The exit is the lifted result: the value the copied chain computes and the tangent the
        // rules computed for it, which is the derivative of the chain at the values the caller
        // passed. Nothing is read back from the original graph: both halves are this graph's.
        ::Tuple *emptyTuple = ::Tuple::create(2, mm::autoSpace());
        const gc_node_ref_t tupleSource = draft.materializeStaticValue(
            camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), lifted);
        const gc_node_ref_t out = draft.addFillNode(
            lifted, rt::makeFillPayload(rt::GCFillKind::Tuple, std::vector<size_t>{0, 1}));
        draft.setNormInputs(out, std::span<const gc_node_ref_t>(&tupleSource, 1));
        const std::array<gc_node_ref_t, 2> halves{chainValue, chainTangent};
        draft.setWithInputs(out, halves);
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    const auto *madeType = made != nullptr ? made->funcType() : nullptr;
    std::string rules;
    for (const std::string &rule : usedRules) {
        rules += (rules.empty() ? "" : ",") + rule;
    }
    os << "REINTERP jvp function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << (madeType != nullptr ? madeType->toString() : std::string("-"))
       << " nodes=" << function->nodeCount() << " kinds=" << copiedKinds << '/' << seenKinds
       << " exit=Tuple{chain,tangent} tangent=computed source=ruleset rules="
       << (rules.empty() ? "-" : rules) << '\n';
    return JvpGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms