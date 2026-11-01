// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "split.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::rtdata::toSlot;
using camel::core::type::FunctionType;
using camel::core::type::param_vec_t;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace {

/// A refusal, said at the node of the source graph that caused it: the construct the plumbing does
/// not copy, and why. A build that answered with a graph anyway would be a graph that computes
/// something else (design principles 9.9).
bool refuseAt(
    SplitGraph &refusal, const rt::GCGraph *source, gc_node_ref_t ref, std::string construct,
    std::string why) {
    refusal.refusedOrigin =
        ref < source->nodeCount()
            ? static_cast<camel::source::origin_id_t>(source->nodeOrigin(ref))
            : camel::source::kInvalidOriginId;
    refusal.refusedConstruct = std::move(construct);
    refusal.refusedWhy       = std::move(why);
    return false;
}

/// The node a value is read through with the gates that only forward it stripped (the same walk
/// `buildVmapGraph` takes to the exit): a gate consumes what it carries and produces that value.
gc_node_ref_t ungated(const rt::GCGraph *graph, gc_node_ref_t ref) {
    while (ref < graph->nodeCount()) {
        const rt::GCNode *node = graph->node(ref);
        if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
            break;
        }
        const std::span<const gc_node_ref_t> norm = graph->normInputsOf(ref);
        if (!norm.empty()) {
            ref = norm.back();
            continue;
        }
        const std::span<const gc_node_ref_t> with = graph->withInputsOf(ref);
        ref = with.empty() ? rt::kInvalidNodeRef : with.back();
    }
    return ref;
}

/// The data ports of `ref`, in port order: the ordinary ones and then the context ones. The effect
/// runs are not here on purpose -- a node that has one is refused, never copied without it.
std::vector<gc_node_ref_t> dataInputsOf(const rt::GCGraph *graph, gc_node_ref_t ref) {
    std::vector<gc_node_ref_t> inputs;
    for (gc_node_ref_t input : graph->normInputsOf(ref)) {
        inputs.push_back(input);
    }
    for (gc_node_ref_t input : graph->withInputsOf(ref)) {
        inputs.push_back(input);
    }
    return inputs;
}

/// A FILL node that makes the tuple `type` out of `members`, in order: the static template is the
/// Norm input (the shape the fill copies) and the values are the context ports. Every writer of a
/// tuple in a runtime graph uses this shape (modules/transforms/jvp.cpp, `liftedPair`).
gc_node_ref_t fillTuple(rt::GraphDraft &draft, Type *type, std::span<const gc_node_ref_t> members) {
    ::Tuple *emptyMembers = ::Tuple::create(members.size(), mm::autoSpace());
    const gc_node_ref_t source =
        draft.materializeStaticValue(toSlot<camel::core::rtdata::Object *>(emptyMembers), type);
    std::vector<size_t> slots(members.size());
    std::iota(slots.begin(), slots.end(), size_t{0});
    const gc_node_ref_t fill =
        draft.addFillNode(type, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
    draft.setNormInputs(fill, std::span<const gc_node_ref_t>(&source, 1));
    draft.setWithInputs(fill, members);
    return fill;
}

/// One half of the split while it is being built: the member's draft, the source graph it copies
/// from, and the port this member gives each parameter of the source (`kInvalidNodeRef` where the
/// half does not take that component -- the forward half does not take tangents, and the backward
/// half reads values only as residuals).
struct Half {
    rt::GraphDraft &draft;
    const rt::GCGraph *source;
    std::vector<gc_node_ref_t> portOf;
    std::unordered_map<gc_node_ref_t, size_t> portIndex;
    std::vector<gc_node_ref_t> copyOf;
    std::vector<char> seen;
    /// Which component of every parameter this half takes: false the value, true the tangent.
    bool tangent = false;
    /// The backward half's residual tuple: the port it arrives by, and one projection per residual,
    /// in the order `residualsOf` numbered them.
    gc_node_ref_t residualsPort = rt::kInvalidNodeRef;
    std::vector<gc_node_ref_t> residualAccs;
    /// What crosses the cut, when this half is the backward one: a source node the forward half
    /// computes and this half reads is *not* rebuilt here -- it is projected out of the residual
    /// tuple at its place in the list. Nothing is recomputed on this side of the cut.
    const std::vector<Residual> *residuals        = nullptr;
    const std::unordered_map<gc_node_ref_t, size_t> *residualIndexOf = nullptr;

    Half(
        rt::GraphDraft &memberDraft, const rt::GCGraph *sourceGraph, bool tangentHalf,
        const std::vector<Residual> *crossing = nullptr,
        const std::unordered_map<gc_node_ref_t, size_t> *crossingIndexOf = nullptr)
        : draft(memberDraft), source(sourceGraph), tangent(tangentHalf), residuals(crossing),
          residualIndexOf(crossingIndexOf) {
        const std::span<const gc_node_ref_t> ports = source->normPorts();
        portOf.assign(ports.size(), rt::kInvalidNodeRef);
        for (size_t i = 0; i < ports.size(); ++i) {
            portIndex[ports[i]] = i;
        }
        copyOf.assign(static_cast<size_t>(source->nodeCount()), rt::kInvalidNodeRef);
        seen.assign(static_cast<size_t>(source->nodeCount()), 0);
    }

    /// The projection of residual `index` out of the tuple this half was handed, made once and shared
    /// by every reader of that value.
    gc_node_ref_t projectResidual(size_t index, SplitGraph &refusal) {
        if (residualsPort == rt::kInvalidNodeRef) {
            refuseAt(
                refusal, source, rt::kInvalidNodeRef, "a residual",
                "the backward half reads a value of the forward half and this build gave it no "
                "residual tuple to read it from");
            return rt::kInvalidNodeRef;
        }
        if (residualAccs.size() <= index) {
            residualAccs.resize(index + 1, rt::kInvalidNodeRef);
        }
        if (residualAccs[index] == rt::kInvalidNodeRef) {
            Type *type = residuals != nullptr && index < residuals->size()
                             ? (*residuals)[index].type
                             : nullptr;
            if (type == nullptr) {
                refuseAt(
                    refusal, source, rt::kInvalidNodeRef, "a residual",
                    "residual " + std::to_string(index) + " has no type to be projected as");
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t accs = draft.addAccsNode(type, static_cast<uint32_t>(index));
            draft.setNormInputs(accs, std::span<const gc_node_ref_t>(&residualsPort, 1));
            residualAccs[index] = accs;
        }
        return residualAccs[index];
    }

    /// Whether `ref` is a value that crosses the cut, so this half takes it from the tuple.
    bool crossesCut(gc_node_ref_t ref) const {
        return residualIndexOf != nullptr && residualIndexOf->find(ref) != residualIndexOf->end();
    }

    /// The copy of one source node, its inputs already copied. The kinds below are the tier the
    /// plumbing copies; every other kind is refused at the node.
    bool makeCopy(gc_node_ref_t ref, SplitGraph &refusal) {
        if (crossesCut(ref)) {
            const gc_node_ref_t accs = projectResidual(residualIndexOf->at(ref), refusal);
            if (accs == rt::kInvalidNodeRef) {
                return false;
            }
            copyOf[ref] = accs;
            return true;
        }
        const rt::GCNode *node = source->node(ref);
        if (node == nullptr) {
            return refuseAt(refusal, source, ref, "a node", "the node is not a node of the graph");
        }
        // The effect runs (control, order, sync) are not data: a node that has one stands in a block
        // or on a chain, and copying its values without its place would move a statement. Refused by
        // name, never dropped.
        if (!source->ctrlInputsOf(ref).empty() || !source->orderPredsOf(ref).empty() ||
            !source->syncPredsOf(ref).empty()) {
            return refuseAt(
                refusal, source, ref, "a node on an effect edge",
                "the plumbing copies the data ports of a straight-line body; the control, order and "
                "sync edges are a later tier");
        }
        // Every input must be available: either copied already, or one of the values that cross the
        // cut, which this half takes out of the residual tuple at its place in the list. A graph the
        // walk could not finish (a cycle, a reference that is not a node) is a refusal, not a node with
        // a hole in its inputs.
        std::vector<gc_node_ref_t> inputs;
        for (gc_node_ref_t input : dataInputsOf(source, ref)) {
            if (input < source->nodeCount() && copyOf[input] == rt::kInvalidNodeRef &&
                crossesCut(input)) {
                const gc_node_ref_t accs = projectResidual(residualIndexOf->at(input), refusal);
                if (accs == rt::kInvalidNodeRef) {
                    return false;
                }
                copyOf[input] = accs;
            }
            if (input >= source->nodeCount() || copyOf[input] == rt::kInvalidNodeRef) {
                return refuseAt(
                    refusal, source, ref, "a node whose input was not copied",
                    "an operand of the node is not reachable from the half's exit");
            }
            inputs.push_back(copyOf[input]);
        }
        switch (node->kind) {
        case rt::GCNodeKind::Port: {
            const auto at = portIndex.find(ref);
            if (at == portIndex.end()) {
                return refuseAt(
                    refusal, source, ref, "a port",
                    "the port is not a parameter of the graph, so neither half takes it");
            }
            const size_t index = at->second;
            if (portOf[index] != rt::kInvalidNodeRef) {
                copyOf[ref] = portOf[index];
                return true;
            }
            // A parameter is a pair and this half takes the other component. The forward half does
            // not take tangents. A value the backward half needs is a residual -- a parameter's value
            // is one like any other -- and `makeCopy` takes those out of the tuple before it gets
            // here, so a parameter port that reaches this point is one the cut does not carry.
            if (!tangent) {
                return refuseAt(
                    refusal, source, ref, "a tangent port",
                    "the value half reads the tangent of parameter " + std::to_string(index / 2) +
                        ", which the forward half does not take");
            }
            return refuseAt(
                refusal, source, ref, "a parameter",
                "the backward half reads the value of parameter " + std::to_string(index / 2) +
                    ", and it is not one of the values the cut carries");
        }
        case rt::GCNodeKind::Data: {
            // A constant stands for the same value in the copy: the slot is written again with the
            // value the source holds (vmap's `liftConstant` does the same for its constants).
            const std::span<const slot_t> slots = source->staticSlots();
            const std::ptrdiff_t index         = -static_cast<std::ptrdiff_t>(node->dataIndex);
            if (node->dataIndex >= 0 || index < 0 || static_cast<size_t>(index) >= slots.size()) {
                return refuseAt(
                    refusal, source, ref, "a constant", "the slot of the constant is not readable");
            }
            copyOf[ref] = draft.materializeStaticValue(
                slots[static_cast<size_t>(index)], node->dataType());
            return true;
        }
        case rt::GCNodeKind::Oper: {
            const auto *body = source->nodeBodyAs<rt::GCOperBody>(ref);
            const gc_node_ref_t made = draft.addOperNode(node->dataType(), nullptr, body->uri());
            draft.setNormInputs(made, inputs);
            copyOf[ref] = made;
            return true;
        }
        case rt::GCNodeKind::Fill: {
            const auto *body = source->nodeBodyAs<rt::GCFillBody>(ref);
            const std::byte *begin = reinterpret_cast<const std::byte *>(body);
            const std::vector<std::byte> payload(
                begin, begin + sizeof(rt::GCFillBody) + body->slots().size_bytes());
            const gc_node_ref_t made = draft.addFillNode(node->dataType(), payload);
            // A fill's data ports are its template (the ordinary one) and the values it carries (the
            // context ones); `inputs` is both, in the source's port order.
            const size_t normCount = source->normInputsOf(ref).size();
            if (normCount > 0) {
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs.data(), normCount));
            }
            if (inputs.size() > normCount) {
                draft.setWithInputs(
                    made,
                    std::span<const gc_node_ref_t>(inputs.data() + normCount, inputs.size() - normCount));
            }
            copyOf[ref] = made;
            return true;
        }
        case rt::GCNodeKind::Gate: {
            // A gate only forwards: what a reader of it reads is its input.
            if (inputs.size() != 1) {
                return refuseAt(
                    refusal, source, ref, "a gate",
                    "the gate forwards no value, or more than one, and the plumbing has no reading "
                    "for either");
            }
            copyOf[ref] = inputs.front();
            return true;
        }
        case rt::GCNodeKind::Call: {
            // A rule's right side reaches the transform as a call: the operands are its ordinary
            // inputs and the rule's own graph is the function value on its context port (`jvp.cpp`
            // builds exactly that shape). The callee is a graph of the program and is not copied, so
            // the call is the same call with its inputs placed on this half's side of the cut.
            const auto *body = source->nodeBodyAs<rt::GCCallBody>(ref);
            const gc_node_ref_t made = body != nullptr
                                           ? draft.addCallNode(node->dataType(), *body)
                                           : draft.addCallNode(node->dataType());
            const size_t normCount = source->normInputsOf(ref).size();
            if (normCount > 0) {
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs.data(), normCount));
            }
            if (inputs.size() > normCount) {
                draft.setWithInputs(
                    made,
                    std::span<const gc_node_ref_t>(
                        inputs.data() + normCount, inputs.size() - normCount));
            }
            copyOf[ref] = made;
            return true;
        }
        case rt::GCNodeKind::Func:
            return refuseAt(
                refusal, source, ref, "a function value",
                "the plumbing copies a straight-line body of ports, constants, primitives, fills and "
                "calls; a function value built by the body is a later tier");
        case rt::GCNodeKind::Brch:
        case rt::GCNodeKind::Join:
            return refuseAt(refusal, source, ref, "a branch", "the plumbing copies straight-line bodies");
        case rt::GCNodeKind::Loop:
            return refuseAt(refusal, source, ref, "a loop", "the plumbing copies straight-line bodies");
        default:
            return refuseAt(
                refusal, source, ref, "a node of another kind",
                std::string("node kind ") + std::to_string(static_cast<int>(node->kind)) +
                    " has no reading in the plumbing tier");
        }
    }

    /// Copies everything `root` needs, inputs first, and answers whether the half is complete.
    bool copyHalf(gc_node_ref_t root, SplitGraph &refusal) {
        if (root >= source->nodeCount()) {
            return refuseAt(refusal, source, root, "the exit", "the exit is not a node");
        }
        seen[root] = 1;
        std::vector<std::pair<gc_node_ref_t, bool>> stack{{root, false}};
        while (!stack.empty()) {
            const auto [ref, expanded] = stack.back();
            if (copyOf[ref] != rt::kInvalidNodeRef) {
                stack.pop_back();
                continue;
            }
            if (!expanded) {
                stack.back().second = true;
                for (gc_node_ref_t input : dataInputsOf(source, ref)) {
                    // A value that crosses the cut is read out of the residual tuple: walking into
                    // what computes it would copy the forward half into this one, which is exactly
                    // the recomputation the cut exists to avoid.
                    if (input >= source->nodeCount() || seen[input] != 0 || crossesCut(input)) {
                        continue;
                    }
                    seen[input] = 1;
                    stack.emplace_back(input, false);
                }
                continue;
            }
            stack.pop_back();
            if (!makeCopy(ref, refusal)) {
                return false;
            }
        }
        return copyOf[root] != rt::kInvalidNodeRef;
    }
};

} // namespace

std::vector<Residual> residualsOf(const rt::GCGraph &lifted) {
    std::vector<Residual> residuals;
    if (lifted.funcType() == nullptr) {
        return residuals;
    }
    // The exit is the pair (result, tangent); anything else is not a lifted graph, and there is no
    // cut to compute residuals of.
    const gc_node_ref_t exit = ungated(&lifted, lifted.exitNodeRef());
    if (exit >= lifted.nodeCount() || lifted.node(exit) == nullptr) {
        return residuals;
    }
    const auto &exitWith = lifted.withInputsOf(exit);
    if (lifted.node(exit)->kind != rt::GCNodeKind::Fill || exitWith.size() != 2) {
        return residuals;
    }
    const gc_node_ref_t resultHalf  = ungated(&lifted, exitWith[0]);
    const gc_node_ref_t tangentHalf = ungated(&lifted, exitWith[1]);

    // Which side of the cut every node is on, as the exit says it: what the result reaches is
    // computed before the cut, what the tangent reaches is computed after it.
    const size_t nodeCount = static_cast<size_t>(lifted.nodeCount());
    std::vector<char> beforeCut(nodeCount, 0);
    {
        std::vector<gc_node_ref_t> stack;
        if (resultHalf < nodeCount) {
            stack.push_back(resultHalf);
            beforeCut[resultHalf] = 1;
        }
        while (!stack.empty()) {
            const gc_node_ref_t ref = stack.back();
            stack.pop_back();
            for (gc_node_ref_t input : dataInputsOf(&lifted, ref)) {
                if (input < nodeCount && beforeCut[input] == 0) {
                    beforeCut[input] = 1;
                    stack.push_back(input);
                }
            }
        }
    }
    // A value the tangent side reads and the result side already computed crosses the cut: it is a
    // residual, and the walk stops at it (what is inside the forward half is not this function's
    // business). A node the result side never reaches is part of the tangent chain: the walk goes
    // through it.
    std::vector<char> visited(nodeCount, 0);
    std::vector<gc_node_ref_t> found;
    {
        std::vector<gc_node_ref_t> stack;
        if (tangentHalf < nodeCount) {
            stack.push_back(tangentHalf);
        }
        while (!stack.empty()) {
            const gc_node_ref_t ref = stack.back();
            stack.pop_back();
            if (ref >= nodeCount || visited[ref] != 0) {
                continue;
            }
            visited[ref] = 1;
            if (beforeCut[ref] != 0 && ref != tangentHalf) {
                found.push_back(ref);
                continue;
            }
            for (gc_node_ref_t input : dataInputsOf(&lifted, ref)) {
                if (input < nodeCount && visited[input] == 0) {
                    stack.push_back(input);
                }
            }
        }
    }
    // Program order: the parameters by their index, then the computed values by their node. The
    // index of a parameter is its place in the graph's port list (`(value, tangent)` pairs), so the
    // value of the k-th parameter is at 2k.
    std::vector<size_t> portIndex(nodeCount, Residual::kNotAParameter);
    const std::span<const gc_node_ref_t> ports = lifted.normPorts();
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i] < nodeCount) {
            portIndex[ports[i]] = i;
        }
    }
    // A graph that says its calls are recomputed on the backward pass (`remat`,
    // kGraphFlagRecomputeBackward, camel/runtime/graph.h) crosses the cut with its parameters and
    // nothing else: every other value is computed again in the backward half from them, because a
    // value that is not a residual is not where Half's walk stops -- it copies what produces it
    // instead (the `crossesCut` branch of copyHalf). The mark says "store less, compute again", and
    // this is the whole of what it buys at the cut: the parameters are what the call is given, and
    // what the body made of them is not carried. The values are the same either way; only the count
    // of what crosses the cut changes, which is why the reading below prints the mark beside it.
    if (lifted.recomputesOnBackward()) {
        std::erase_if(found, [&portIndex](gc_node_ref_t ref) {
            return portIndex[ref] == Residual::kNotAParameter;
        });
    }
    std::sort(found.begin(), found.end(), [&portIndex](gc_node_ref_t a, gc_node_ref_t b) {
        const bool aIsPort = portIndex[a] != Residual::kNotAParameter;
        const bool bIsPort = portIndex[b] != Residual::kNotAParameter;
        if (aIsPort != bIsPort) {
            return aIsPort;
        }
        if (aIsPort) {
            return portIndex[a] < portIndex[b];
        }
        return a < b;
    });
    residuals.reserve(found.size());
    for (gc_node_ref_t ref : found) {
        Residual residual;
        residual.node = ref;
        const size_t index = portIndex[ref];
        if (index != Residual::kNotAParameter) {
            // A parameter's value: its type is the parameter's, and its number is half its index.
            residual.parameter = index / 2;
            residual.type      = index < lifted.funcType()->normTypesCount()
                                     ? lifted.funcType()->normTypeAt(index)
                                     : lifted.node(ref)->dataType();
        } else {
            residual.parameter = Residual::kNotAParameter;
            residual.type      = lifted.node(ref) != nullptr ? lifted.node(ref)->dataType() : nullptr;
        }
        residuals.push_back(residual);
    }
    return residuals;
}

SplitHalves splitHalvesOf(FunctionType *function) { return splitHalvesWith(function, nullptr); }

SplitHalves splitHalvesWith(FunctionType *function, Type *residuals) {
    SplitHalves halves;
    if (function == nullptr) {
        halves.why = "there is no function to split";
        return halves;
    }
    // One body is copied into two graphs, so no captured parameter: a `with` value would have a port
    // in neither half (the same shape `jvp` and `vmap` state).
    if (function->withTypesCount() != 0) {
        halves.why = "`split` takes a function without `with` parameters: the transform copies the "
                     "body into two new graphs, and a captured value has no port to enter them by";
        return halves;
    }
    const size_t count = function->normTypesCount();
    if (count == 0 || count % 2 != 0) {
        halves.why = "`split` takes a lifted function, whose every parameter is the pair "
                     "(value, tangent) and so comes in twos";
        return halves;
    }
    if (!function->hasExitType() || function->exitType()->code() != TypeCode::Tuple) {
        halves.why = "`split` takes a lifted function, whose result is the pair (result, tangent)";
        return halves;
    }
    const auto *exitPair = tt::as_ptr<TupleType>(function->exitType());
    if (exitPair->size() != 2) {
        halves.why = "`split` takes a lifted function, whose result is the pair (result, tangent); "
                     "this one answers " +
                     std::to_string(exitPair->size()) + " values";
        return halves;
    }
    Type *result  = exitPair->typeAt(0);
    Type *tangent = exitPair->typeAt(1);
    if (result == nullptr || tangent == nullptr) {
        halves.why = "the pair a lifted function answers with has a component without a type";
        return halves;
    }

    // The residuals: the values the backward half reads, in program order. Which values those are is a
    // property of the *graph*, so the graph answers it (`residualsOf`) and the tuple it names is the
    // one the halves are typed with when the graph is in hand. A caller that has no graph -- the
    // operator resolver, which sees types only -- gets a stand-in: one member per value parameter.
    // It is the type a call site is checked against and nothing else; it is not a reading, and the
    // count a reading states comes from the graph (`residuals=`).
    if (residuals == nullptr) {
        std::vector<Type *> residualTypes;
        for (size_t i = 0; i < count; i += 2) {
            residualTypes.push_back(function->normTypeAt(i));
        }
        residuals = TupleType::create(residualTypes);
    }

    param_vec_t forwardParams;
    param_vec_t tangentParams;
    for (size_t i = 0; i < count; i += 2) {
        forwardParams.emplace_back(function->normTypeAt(i), false);
        tangentParams.emplace_back(function->normTypeAt(i + 1), false);
    }
    param_vec_t backwardParams;
    backwardParams.emplace_back(residuals, false);
    backwardParams.insert(backwardParams.end(), tangentParams.begin(), tangentParams.end());

    TupleType *forwardExit = TupleType::create(std::vector<Type *>{result, residuals});
    // Each half does part of what the source does to the outside world, so each publishes the
    // source's effects: a half whose print nobody depends on does not run at all.
    halves.forward = FunctionType::create(param_vec_t{}, forwardParams, forwardExit)
                         ->withResources(function->resources());
    halves.backward = FunctionType::create(param_vec_t{}, backwardParams, tangent)
                          ->withResources(function->resources());
    halves.pair = TupleType::create(std::vector<Type *>{halves.forward, halves.backward});
    return halves;
}

SplitGraph buildSplitGraph(
    const camel::core::context::context_ptr_t &context, const rt::Program *program,
    rt::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return SplitGraph{};
    }
    // What crosses the cut, read from the graph before either half exists: the residual tuple's
    // members are these values, in this order, and the two halves are typed with it. Nothing here
    // asks a signature how many residuals there are -- a signature is computed from types, and types
    // cannot tell `f(x) = x` (no residuals) from `f(x) = x*x` (one), which have the same lifted type.
    const std::vector<Residual> graphResiduals = residualsOf(*function);
    std::vector<Type *> residualTypes;
    residualTypes.reserve(graphResiduals.size());
    for (const Residual &residual : graphResiduals) {
        if (residual.type == nullptr) {
            os << "REINTERP split function=" << function->name()
               << " skip=residual (" << "a value the backward half reads has no type" << ")\n";
            return SplitGraph{};
        }
        residualTypes.push_back(residual.type);
    }
    Type *residualsType = TupleType::create(residualTypes);
    const SplitHalves halves = splitHalvesWith(function->funcType(), residualsType);
    if (halves.forward == nullptr || halves.backward == nullptr) {
        os << "REINTERP split function=" << function->name() << " skip=signature (" << halves.why
           << ")\n";
        return SplitGraph{};
    }
    // The exit of a lifted function is the pair (result, tangent): the first component is what the
    // forward half computes, the second is where the tangent comes from.
    const gc_node_ref_t exit = ungated(function, function->exitNodeRef());
    if (exit >= function->nodeCount() || function->node(exit) == nullptr) {
        os << "REINTERP split function=" << function->name() << " skip=exit\n";
        return SplitGraph{};
    }
    const rt::GCNode *exitNode = function->node(exit);
    const auto *exitFill =
        exitNode->kind == rt::GCNodeKind::Fill ? function->nodeBodyAs<rt::GCFillBody>(exit)
                                               : nullptr;
    if (exitFill == nullptr || exitFill->fillKind != rt::GCFillKind::Tuple ||
        function->withInputsOf(exit).size() != 2) {
        SplitGraph refusal;
        refuseAt(
            refusal, function, exit, "the exit",
            "a lifted function answers with the pair (result, tangent); this graph's exit is not a "
            "two-member tuple fill, so there is no pair to cut");
        os << "REINTERP split function=" << function->name() << " skip=exit ("
           << refusal.refusedWhy << ")\n";
        return refusal;
    }
    const gc_node_ref_t resultHalf  = ungated(function, function->withInputsOf(exit)[0]);
    const gc_node_ref_t tangentHalf = ungated(function, function->withInputsOf(exit)[1]);

    const std::span<const gc_node_ref_t> sourcePorts = function->normPorts();
    if (sourcePorts.size() != function->funcType()->normTypesCount()) {
        os << "REINTERP split function=" << function->name() << " skip=ports ("
           << sourcePorts.size() << " ports for " << function->funcType()->normTypesCount()
           << " parameters)\n";
        return SplitGraph{};
    }

    const std::string base = "split_of_" + std::string(function->name());
    rt::GraphDraftGroup group(context, program);
    // Both members are reserved before either is built: they are the two graphs of one call, and
    // the first reserved one is the group's root (the forward half, which a caller runs first).
    const GraphId forwardId  = group.reserve(base + "_forward", base + "_forward");
    const GraphId backwardId = group.reserve(base + "_backward", base + "_backward");

    SplitGraph built;
    {
        // The forward half: the value of every parameter in, (result, residuals) out.
        auto &draft = group.transaction(forwardId).edit();
        draft.setFuncType(halves.forward);
        Half forward(draft, function, false);
        for (size_t i = 0; i < sourcePorts.size(); i += 2) {
            Type *type       = function->funcType()->normTypeAt(i);
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            forward.portOf[i] = port;
        }
        if (!forward.copyHalf(resultHalf, built)) {
            os << "REINTERP split function=" << function->name()
               << " skip=forward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        // The residuals: the values `residualsOf` named, in its order -- the forward half computes them all
        // (they are computed before the cut, that is what makes them residuals) and hands them on as
        // one tuple. A parameter's value is one of them when the backward half reads it; the identity
        // graph reads nothing, so its tuple is empty.
        std::vector<gc_node_ref_t> residualMembers;
        residualMembers.reserve(graphResiduals.size());
        for (const Residual &residual : graphResiduals) {
            if (residual.node >= function->nodeCount() ||
                forward.copyOf[residual.node] == rt::kInvalidNodeRef) {
                os << "REINTERP split function=" << function->name()
                   << " skip=residual (the forward half does not compute a value the cut carries)\n";
                return built;
            }
            residualMembers.push_back(forward.copyOf[residual.node]);
        }
        const gc_node_ref_t residualValue = fillTuple(
            draft, halves.backward->normTypeAt(0),
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{forward.copyOf[resultHalf], residualValue};
        const gc_node_ref_t out = fillTuple(
            draft, halves.forward->exitType(),
            std::span<const gc_node_ref_t>(exitHalves.data(), exitHalves.size()));
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the residuals and the tangent of every parameter in, the tangent out.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(halves.backward);
        std::unordered_map<gc_node_ref_t, size_t> residualIndexOf;
        for (size_t i = 0; i < graphResiduals.size(); ++i) {
            residualIndexOf[graphResiduals[i].node] = i;
        }
        Half backward(draft, function, true, &graphResiduals, &residualIndexOf);
        Type *residualsType         = halves.backward->normTypeAt(0);
        const gc_node_ref_t residualsPort =
            draft.addPortNode(residualsType, draft.allocateRuntimeSlot(residualsType));
        draft.appendNormPort(residualsPort);
        backward.residualsPort = residualsPort;
        for (size_t i = 1; i < sourcePorts.size(); i += 2) {
            Type *type       = function->funcType()->normTypeAt(i);
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            backward.portOf[i] = port;
        }
        if (!backward.copyHalf(tangentHalf, built)) {
            os << "REINTERP split function=" << function->name()
               << " skip=backward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        const gc_node_ref_t out = backward.copyOf[tangentHalf];
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    group.encode();
    const rt::GCGraph *forwardMade  = group.encoded(forwardId);
    const rt::GCGraph *backwardMade = group.encoded(backwardId);
    built.residuals                 = graphResiduals.size();
    os << "REINTERP split function=" << function->name() << " forward=" << rt::toValue(forwardId)
       << " backward=" << rt::toValue(backwardId)
       << " type=" << (forwardMade != nullptr && forwardMade->funcType() != nullptr
                           ? forwardMade->funcType()->toString()
                           : std::string("-"))
       << " residuals=" << built.residuals;
    // Printed only when the graph being cut carries the mark, so every other cut keeps the line it
    // had: `remat`'s own line says the property was set, and this one says what the cut did with it.
    if (function->recomputesOnBackward()) {
        os << " recompute=1";
    }
    os << '\n';
    built.program  = group.program();
    built.forward  = const_cast<rt::GCGraph *>(forwardMade);
    built.backward = const_cast<rt::GCGraph *>(backwardMade);
    return built;
}

} // namespace camel::transforms