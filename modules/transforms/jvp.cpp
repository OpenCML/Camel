// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "jvp.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <array>
#include <span>
#include <string>
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
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "jvp_of_" + std::string(function->name()), "jvp_of_" + std::string(function->name()));
    {
        auto &draft = group.draft(member);
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
                // reads is the value the caller passes. The tangent is the second parameter and has
                // no node in the original body: no primitive computes it yet.
                if (!function->normPorts().empty() && function->normPorts().front() == ref) {
                    copyOf[ref] = value;
                    copied[ref] = true;
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
                    copied[ref] = true;
                    ++copiedKinds;
                }
                break;
            }
            case rt::GCNodeKind::Oper: {
                const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const gc_node_ref_t made =
                    draft.addOperNode(node->dataType(), nullptr, body->uri());
                std::vector<gc_node_ref_t> inputs;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    // A primitive whose input was not copied cannot be reproduced: the copy would
                    // name a node that is not there, and the transform would answer with a graph
                    // that computes something else. The build says so and gives nothing.
                    if (input >= function->nodeCount() || !copied[input]) {
                        complete = false;
                        break;
                    }
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
        // The exit is the lifted result, and nothing else: the value the copied chain computes and
        // the tangent the caller passed. No primitive has been rewritten yet, so this graph is the
        // identity in two parts -- it establishes the shape of the new function, not the derivative.
        ::Tuple *emptyTuple = ::Tuple::create(2, mm::autoSpace());
        const gc_node_ref_t tupleSource = draft.materializeStaticValue(
            camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), lifted);
        const gc_node_ref_t out = draft.addFillNode(
            lifted, rt::makeFillPayload(rt::GCFillKind::Tuple, std::vector<size_t>{0, 1}));
        draft.setNormInputs(out, std::span<const gc_node_ref_t>(&tupleSource, 1));
        const std::array<gc_node_ref_t, 2> halves{chainValue, tangent};
        draft.setWithInputs(out, halves);
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    const auto *madeType = made != nullptr ? made->funcType() : nullptr;
    os << "REINTERP jvp function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << (madeType != nullptr ? madeType->toString() : std::string("-"))
       << " nodes=" << function->nodeCount() << " kinds=" << copiedKinds << '/' << seenKinds
       << " exit=Tuple{chain,tangent} tangent=caller's(not computed)\n";
    return JvpGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms