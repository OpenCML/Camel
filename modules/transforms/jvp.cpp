// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "jvp.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

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
        auto &draft = group.transaction(member).edit();
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
        // Step 3, second layer: a tangent for every node, computed by one rule per primitive. A
        // constant's tangent is zero, a port's is the tangent parameter the caller passed, and a
        // primitive's is the rule below applied to its operands' values and tangents. The rules are
        // the minimal set `f` needs; a primitive with none is refused *by name* rather than
        // answered with 0 or 1, because a wrong derivative that looks like a derivative is the
        // expensive kind of wrong.
        std::vector<gc_node_ref_t> tangentOf(function->nodeCount(), rt::kInvalidNodeRef);
        gc_node_ref_t zeroConstant = rt::kInvalidNodeRef;
        const auto primitive = [&draft](const char *uri, std::vector<gc_node_ref_t> inputs) {
            const gc_node_ref_t made = draft.addOperNode(Type::Float64(), nullptr, uri);
            draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
            return made;
        };
        const auto noteRule = [&usedRules](const char *name) {
            if (std::find(usedRules.begin(), usedRules.end(), name) == usedRules.end()) {
                usedRules.emplace_back(name);
            }
        };
        // One writing per law, matching stdlib/rulesets/jvp.cml line for line (and the tangent
        // functions camel::autodiff exports): `jvp_add`, `jvp_sub`, `jvp_mul`, `jvp_div`. Reading
        // them from the ruleset's index instead -- `forwardRuleOf` and the rule's right side -- is
        // the road forward.h describes, and it needs two things this transform does not have yet:
        // the ruleset is in the index only when the program imports `rulesets.jvp`, and binding the
        // right side's captured operands (`x`, `w`) is a closure the graph has to build at run time.
        // `ref` is the rule's value in the new graph, `originals` the operands in the old one (where
        // the tangents are kept) and `inputs` the same operands in the new one.
        const auto tangentRule = [&](gc_node_ref_t ref, std::string_view uri,
                                     const std::vector<gc_node_ref_t> &originals,
                                     const std::vector<gc_node_ref_t> &inputs) -> gc_node_ref_t {
            if (originals.size() != 2) {
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t a = inputs[0];
            const gc_node_ref_t b = inputs[1];
            const gc_node_ref_t da = tangentOf[originals[0]];
            const gc_node_ref_t db = tangentOf[originals[1]];
            if (uri == ":op/add_d") {
                noteRule("add");
                return primitive(":op/add_d", {da, db});
            }
            if (uri == ":op/sub_d") {
                noteRule("sub");
                return primitive(":op/sub_d", {da, db});
            }
            if (uri == ":op/mul_d") {
                // (a*b)' = a'*b + a*b'
                noteRule("mul");
                const gc_node_ref_t lhs = primitive(":op/mul_d", {da, b});
                const gc_node_ref_t rhs = primitive(":op/mul_d", {a, db});
                return primitive(":op/add_d", {lhs, rhs});
            }
            if (uri == ":op/div_d") {
                // (a/b)' = (a' - (a/b)*b') / b, with (a/b) the value the copy already computes
                noteRule("div");
                const gc_node_ref_t shift = primitive(":op/mul_d", {copyOf[ref], db});
                const gc_node_ref_t top   = primitive(":op/sub_d", {da, shift});
                return primitive(":op/div_d", {top, b});
            }
            return rt::kInvalidNodeRef;
        };
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
                tangentOf[ref] = tangentRule(ref, uri, originals, inputs);
                if (tangentOf[ref] == rt::kInvalidNodeRef) {
                    // Named, not guessed: the transform has no derivative for this operator, and
                    // the caller is told which one.
                    os << "REINTERP jvp function=" << function->name() << " skip=rule uri=" << uri
                       << '\n';
                    return JvpGraph{};
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
       << " exit=Tuple{chain,tangent} tangent=computed rules=" << (rules.empty() ? "-" : rules)
       << '\n';
    return JvpGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms