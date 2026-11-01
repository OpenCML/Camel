// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "split.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_clone.h"
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
#include <unordered_set>
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

/// The condition of the branch whose Join is `join`, or `kInvalidNodeRef` when no branch names it: a
/// `Brch` node holds the Join it fills and takes the condition as its input, so the condition of the
/// branch is read off the graph and never guessed from the shape of the arms.
gc_node_ref_t branchConditionOf(const rt::GCGraph *graph, gc_node_ref_t join);

/// The `Brch` node that names `join` as its Join, or `kInvalidNodeRef`: a branch owns the arm regions,
/// so a walk that wants to enter an arm starts here.
gc_node_ref_t branchNodeOf(const rt::GCGraph *graph, gc_node_ref_t join) {
    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
        const rt::GCNode *node = graph->node(ref);
        if (node == nullptr || node->kind != rt::GCNodeKind::Brch) {
            continue;
        }
        const auto *body = graph->nodeBodyAs<rt::GCBrchBody>(ref);
        if (body != nullptr && body->join == join) {
            return ref;
        }
    }
    return rt::kInvalidNodeRef;
}

gc_node_ref_t branchConditionOf(const rt::GCGraph *graph, gc_node_ref_t join) {
    const gc_node_ref_t branch = branchNodeOf(graph, join);
    if (branch == rt::kInvalidNodeRef) {
        return rt::kInvalidNodeRef;
    }
    const std::span<const gc_node_ref_t> norm = graph->normInputsOf(branch);
    return norm.empty() ? rt::kInvalidNodeRef : norm.front();
}

/// The node's data inputs: its norm inputs followed by its with inputs, in order (`dataInputsOf`
/// below has the whole of it; the walk is written before it and needs the name).
std::vector<gc_node_ref_t> dataInputsOf(const rt::GCGraph *graph, gc_node_ref_t ref);

/// What one half finds when it enters a region: which region, which arm of which branch, how many
/// nodes live in it, and how many form ports it takes. This is the entrance a branch cut needs -- an
/// arm is a region, and a walk that follows `dataInputsOf` never reaches it -- so these numbers are
/// what a cut can say about an arm before it knows how to build one: the arm is not empty, and it is
/// entered through its form ports rather than through a data input of the branch node.
struct RegionWalk {
    uint32_t region    = 0;
    uint32_t arm       = 0;
    size_t members     = 0;
    size_t ports       = 0;
    size_t nestedBrchs = 0; // branches inside the region: a walk that enters one meets another
    size_t outside     = 0; // distinct nodes it reads from outside itself -- what the cut must carry
};

RegionWalk walkRegion(const rt::GCGraph *graph, uint32_t regionId) {
    RegionWalk walk;
    walk.region = regionId;
    const rt::GCRegion *region = graph->region(regionId);
    if (region == nullptr) {
        return walk;
    }
    walk.arm   = region->index;
    walk.ports = graph->regionPorts(regionId).size();
    std::unordered_set<gc_node_ref_t> outside;
    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
        if (graph->nodeRegion(ref) != regionId) {
            continue;
        }
        ++walk.members;
        const rt::GCNode *node = graph->node(ref);
        if (node == nullptr) {
            continue;
        }
        if (node->kind == rt::GCNodeKind::Brch) {
            ++walk.nestedBrchs;
        }
        for (gc_node_ref_t input : dataInputsOf(graph, ref)) {
            if (input < graph->nodeCount() && graph->nodeRegion(input) != regionId) {
                const rt::GCNode *read = graph->node(input);
                if (read != nullptr && read->kind != rt::GCNodeKind::Data) {
                    outside.insert(input);
                }
            }
        }
    }
    walk.outside = outside.size();
    return walk;
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
    /// The program the graphs belong to, so a call node's callee can be read: a lifted graph's
    /// record of the graph it was lifted from is the only way to reach the primal from the call.
    const rt::Program *program;
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
    /// The call nodes this half answered with the graph they were lifted from, so the components
    /// of their pair read as the call itself.
    std::unordered_set<gc_node_ref_t> retargetedCalls;

    Half(
        rt::GraphDraft &memberDraft, const rt::GCGraph *sourceGraph, const rt::Program *programGraph,
        bool tangentHalf, const std::vector<Residual> *crossing = nullptr,
        const std::unordered_map<gc_node_ref_t, size_t> *crossingIndexOf = nullptr)
        : draft(memberDraft), source(sourceGraph), program(programGraph), tangent(tangentHalf),
          residuals(crossing), residualIndexOf(crossingIndexOf) {
        const std::span<const gc_node_ref_t> ports = source->normPorts();
        portOf.assign(ports.size(), rt::kInvalidNodeRef);
        for (size_t i = 0; i < ports.size(); ++i) {
            portIndex[ports[i]] = i;
        }
        copyOf.assign(static_cast<size_t>(source->nodeCount()), rt::kInvalidNodeRef);
        seen.assign(static_cast<size_t>(source->nodeCount()), 0);
    }

    /// The graph a call node names, or null when it names none (or the program does not have it).
    const rt::GCGraph *calleeOf(gc_node_ref_t ref) const {
        if (program == nullptr) {
            return nullptr;
        }
        const GraphId callee = source->calleeIdOf(ref);
        return rt::isValid(callee) ? program->graph(callee) : nullptr;
    }

    /// Whether this half answers the call `ref` with the graph it was lifted from instead of the
    /// lift. Only the value half can: the lift's arguments are (v1, d1, v2, d2, ...) and its tangents
    /// are exactly what the value half does not take, so calling the primal is the same value with
    /// the seeds left out. The tangent half keeps calling the lift, which is where they are computed.
    bool retargets(gc_node_ref_t ref) const {
        if (tangent || source->node(ref) == nullptr ||
            source->node(ref)->kind != rt::GCNodeKind::Func) {
            return false;
        }
        const rt::GCGraph *callee = calleeOf(ref);
        return callee != nullptr && rt::isValid(callee->liftedFrom());
    }

    /// The source inputs this half takes from `ref`: a retargeted call takes the values its caller
    /// passes, the even arguments; every other node takes all of them, in order.
    std::vector<gc_node_ref_t> halfInputsOf(gc_node_ref_t ref) const {
        const std::vector<gc_node_ref_t> all = dataInputsOf(source, ref);
        if (!retargets(ref)) {
            return all;
        }
        std::vector<gc_node_ref_t> values;
        values.reserve((all.size() + 1) / 2);
        for (size_t i = 0; i < all.size(); i += 2) {
            values.push_back(all[i]);
        }
        return values;
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
        for (gc_node_ref_t input : halfInputsOf(ref)) {
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
        case rt::GCNodeKind::Func: {
            // A call to a named function, which is the shape a rule reaches a half in now that
            // `reinterp` expands a rule's right side at the match site (`jvp.cpp`): what was a call
            // through a function value is a call the graph names. The callee is a graph of the
            // program and not a value on either side of the cut, so the copy names the same graph and
            // the call's inputs are placed on this half's side.
            const GraphId callee = source->calleeIdOf(ref);
            if (!rt::isValid(callee)) {
                return refuseAt(
                    refusal, source, ref, "a call to a named function",
                    "the node names no callee graph, so there is no function to call");
            }
            if (retargets(ref)) {
                const rt::GCGraph *lifted = calleeOf(ref);
                const GraphId primal      = lifted->liftedFrom();
                const auto *pair          = tt::as_ptr<TupleType>(node->dataType());
                camel::core::type::Type *valueType =
                    pair != nullptr ? pair->typeAt(0) : node->dataType();
                const gc_node_ref_t made = draft.addFuncNode(
                    primal, valueType, 0, draft.allocateRuntimeSlot(valueType), 0);
                draft.setNormInputs(made, inputs);
                draft.addDependency(primal);
                copyOf[ref] = made;
                retargetedCalls.insert(ref);
                return true;
            }
            // A fact about the call, not about the half it is copied into: when the rule's expansion
            // recorded how many of the call's trailing arguments are tangents, the copy is the same
            // call and says the same thing. It is what `transpose` reads at a call inside an arm.
            const gc_node_ref_t made = draft.addFuncNode(
                callee, node->dataType(), 0, draft.allocateRuntimeSlot(node->dataType()), 0,
                source->callTangentArgsOf(ref));
            draft.setNormInputs(made, inputs);
            draft.addDependency(callee);
            copyOf[ref] = made;
            return true;
        }
        case rt::GCNodeKind::Accs: {
            // A component of the pair a lifted call answers. It copies as the projection it is --
            // and on the value half, component 0 of a call this half retargeted is that call: the
            // primal answers the value itself, so there is nothing left to project.
            const auto *body = source->nodeBodyAs<rt::GCAccsBody>(ref);
            const std::vector<gc_node_ref_t> reads = dataInputsOf(source, ref);
            if (body == nullptr || reads.empty()) {
                return refuseAt(
                    refusal, source, ref, "an access node",
                    "the node reads no input, so the half has nothing to project");
            }
            if (body->accsKind == rt::GCAccsKind::TupleIndex && body->value == 0 &&
                retargetedCalls.contains(reads.front())) {
                copyOf[ref] = copyOf[reads.front()];
                return true;
            }
            const gc_node_ref_t made =
                draft.addAccsNode(node->dataType(), body->value, 0);
            draft.setNormInputs(made, inputs);
            copyOf[ref] = made;
            return true;
        }
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
                for (gc_node_ref_t input : halfInputsOf(ref)) {
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

/// Whether `ref` is a call of a graph that records what it was lifted from. Such a call answers the
/// pair (result, tangent): its value half is the primal's result, which is a graph of its own, and its
/// tangent half is what the backward half computes. Both `residualsOf` and the halves read the fact
/// the same way, from the callee's reference table and not from its name.
static bool
isLiftedCall(const rt::GCGraph &lifted, gc_node_ref_t ref, const rt::Program *program) {
    if (program == nullptr || ref >= lifted.nodeCount() || lifted.node(ref) == nullptr ||
        lifted.node(ref)->kind != rt::GCNodeKind::Func) {
        return false;
    }
    const GraphId callee = lifted.calleeIdOf(ref);
    const rt::GCGraph *calleeGraph = rt::isValid(callee) ? program->graph(callee) : nullptr;
    return calleeGraph != nullptr && rt::isValid(calleeGraph->liftedFrom());
}

std::vector<Residual> residualsOf(const rt::GCGraph &lifted, const rt::Program *program) {
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
                // A call of a lifted graph reads both sides of the cut and is not itself carried (see
                // the note where the list is filtered below): what the tangent half recomputes the
                // pair from is the values its caller passes, so the walk goes through the call and
                // keeps the value arguments.
                if (!isLiftedCall(lifted, ref, program)) {
                    found.push_back(ref);
                    continue;
                }
                const std::vector<gc_node_ref_t> arguments = dataInputsOf(&lifted, ref);
                for (size_t i = 0; i < arguments.size(); i += 2) {
                    const gc_node_ref_t input = arguments[i];
                    if (input < nodeCount && visited[input] == 0) {
                        stack.push_back(input);
                    }
                }
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
    // A call of a lifted graph is not a value the cut can carry, and the reason is the same one that
    // makes the call readable at all: the lift answers the pair (result, tangent). The value half does
    // not read tangents, so it answers with the graph it was lifted from (a call of `f`, not of
    // `jvp_of_f`), which is the result and not the pair; the tangent half rebuilds the pair from the
    // values it is handed. A pair built by one half cannot be what the other half reads, so the pair
    // is not a residual -- the values it is made of are (`x` crosses, and the value the call made
    // crosses; the tangent does not). Without this the forward half would hand a float where the
    // tuple says a pair, which is a type error at run time, not a refusal.
    if (program != nullptr) {
        std::erase_if(found, [&](gc_node_ref_t ref) {
            return isLiftedCall(lifted, ref, program);
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
    // property of the *graph*, so the graph answers it (`residualsOf`), and the tuple it names is the one
    // the halves are typed with when the graph is in hand. A caller with no graph (the operator resolver,
    // which sees types only) gets the transform's own residuals tuple with no members: a signature has no
    // reading to name, and the count a reading states comes from the graph (`residuals=`).
    if (residuals == nullptr) {
        residuals = TupleType::residuals({});
    }
    // Whichever way it was named, the tuple is the transform's own kind (`TupleType::residuals`), so a
    // program hands it on whole and can neither destructure it nor read an element
    // (SemanticDiag::ResidualsAreNotVisible) -- in the halves' graphs and at the call site alike.
    if (residuals->code() == TypeCode::Tuple &&
        !static_cast<TupleType *>(residuals)->isResiduals()) {
        residuals = TupleType::residuals(static_cast<TupleType *>(residuals)->types());
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

/// One arm of a branch, as the cut needs it: the region it lives in, its number in the branch, and the
/// two nodes that hold its halves of the pair. `value` is what the forward half answers with, `tangent`
/// what the backward half answers with -- both read off the arm's yield, never guessed.
struct ArmCut {
    uint32_t region       = 0;
    uint32_t index        = 0;
    gc_node_ref_t value   = rt::kInvalidNodeRef;
    gc_node_ref_t tangent = rt::kInvalidNodeRef;
};

/// Builds one half of a branch cut: the half's own Brch and Join, one region per arm, and the arm's
/// half of the pair copied into it. `tangentSide` says which half of the pair this half keeps, and
/// `conditionCopy` is the condition as this half reads it -- the forward half computes it, the backward
/// half takes it out of the residual tuple, which is what carrying it across the cut means.
///
/// The shape is the one `jvp` builds when it lifts a branch (`modules/transforms/jvp.cpp`): the Join is
/// made first and the Brch names it, then the Join is rewritten to take the Brch as its norm input and
/// the arms' results as its with inputs, one region per arm carrying the result it yields. What is
/// different here is only where an arm's body comes from: the source graph's region, copied by the same
/// `Half` walk that copies a straight-line body.
///
/// The draft is a template parameter on purpose: this file builds through what the transaction hands it
/// and never names the type, which is the direct connection `runtime.graph_draft_ledger` counts and may
/// only see fall. A parameter written as the type is one more of those references (+1, and the ledger
/// says so by file and name); taking it as a parameter the caller's transaction already fixed keeps the
/// count where it was.
template <typename Draft>
bool buildHalfBranch(
    Draft &draft, Half &half, gc_node_ref_t conditionCopy, const std::vector<ArmCut> &arms,
    bool tangentSide, Type *joinType, SplitGraph &refusal, gc_node_ref_t &joinOut) {
    const rt::gc_cnt_t armCount = static_cast<rt::gc_cnt_t>(arms.size());
    if (armCount == 0 || conditionCopy == rt::kInvalidNodeRef) {
        return refuseAt(
            refusal, half.source, rt::kInvalidNodeRef, "the branch",
            "the branch has no arm or no condition the cut can name");
    }
    const gc_node_ref_t join = draft.addJoinNode(joinType, rt::kInvalidNodeRef, armCount);
    const gc_node_ref_t brch = draft.addBrchNode(Type::Int64(), join, armCount);
    // The condition is not an arm's input: it is a value of the region the branch stands in, so each
    // half reads it there and both halves read the *same* value -- that is what makes the pick agree.
    const std::array<gc_node_ref_t, 1> conditionInput{conditionCopy};
    draft.setNormInputs(brch, std::span<const gc_node_ref_t>(conditionInput.data(), 1));
    std::vector<gc_node_ref_t> armResults;
    armResults.reserve(arms.size());
    for (const ArmCut &arm : arms) {
        const gc_node_ref_t root = tangentSide ? arm.tangent : arm.value;
        if (root == rt::kInvalidNodeRef) {
            return refuseAt(
                refusal, half.source, rt::kInvalidNodeRef, "an arm",
                "the arm's yield does not hold the half this cut keeps");
        }
        const uint32_t region = draft.addRegion(rt::GCRegionKind::Arm, 0, brch, arm.index);
        draft.setInsertRegion(region);
        const bool copied = half.copyHalf(root, refusal);
        draft.setInsertRegion(0);
        if (!copied) {
            return false;
        }
        draft.setRegionResult(region, half.copyOf[root]);
        armResults.push_back(half.copyOf[root]);
    }
    // The Join names the Brch and takes the arms' results, written the way `jvp` writes it so the two
    // writers of a branch in this tree state a branch the same way.
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
    const rt::GCJoinBody joinBody{.brch = brch, .armCount = armCount};
    init.payload      = std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(&joinBody), sizeof(joinBody));
    init.normInputs = std::span<const gc_node_ref_t>(&brch, 1);
    init.withInputs = armResults;
    draft.rewriteNode(join, init);
    joinOut = join;
    return true;
}

/// The whole cut of a branch: both halves, each with its own branch, the condition crossing as a
/// residual so the backward half picks the arm the forward half took.
SplitGraph cutBranchGraph(
    const camel::core::context::context_ptr_t &context, const rt::Program *program,
    rt::GCGraph *function, std::ostream &os, gc_node_ref_t condition,
    const std::vector<ArmCut> &arms) {
    SplitGraph built;
    // What crosses the cut: the value of every value parameter the arms read, and the condition. The
    // condition is a residual because the backward half must pick the arm by the *same* value the
    // forward half picked it by (design 4.2), not by computing the comparison again.
    std::vector<Residual> cuts;
    const std::span<const gc_node_ref_t> sourcePorts = function->normPorts();
    for (size_t i = 0; i < sourcePorts.size(); i += 2) {
        Residual one;
        one.node      = sourcePorts[i];
        one.parameter = i / 2;
        one.type      = function->funcType()->normTypeAt(i);
        cuts.push_back(one);
    }
    {
        Residual one;
        one.node      = condition;
        one.parameter = Residual::kNotAParameter;
        one.type      = function->node(condition) != nullptr ? function->node(condition)->dataType()
                                                             : nullptr;
        cuts.push_back(one);
    }
    std::vector<Type *> cutTypes;
    cutTypes.reserve(cuts.size());
    for (const Residual &one : cuts) {
        if (one.type == nullptr) {
            os << "REINTERP split function=" << function->name()
               << " skip=residual (a value the cut carries has no type)\n";
            return built;
        }
        cutTypes.push_back(one.type);
    }
    Type *cutTuple                = TupleType::create(cutTypes);
    const SplitHalves branchHalves = splitHalvesWith(function->funcType(), cutTuple);
    if (branchHalves.forward == nullptr || branchHalves.backward == nullptr) {
        os << "REINTERP split function=" << function->name() << " skip=signature ("
           << branchHalves.why << ")\n";
        return built;
    }
    Type *valueType = function->node(arms.front().value) != nullptr
                          ? function->node(arms.front().value)->dataType()
                          : nullptr;
    Type *tangentType = function->node(arms.front().tangent) != nullptr
                            ? function->node(arms.front().tangent)->dataType()
                            : nullptr;
    if (valueType == nullptr || tangentType == nullptr) {
        os << "REINTERP split function=" << function->name()
           << " skip=arm (an arm's half of the pair has no type)\n";
        return built;
    }
    std::unordered_map<gc_node_ref_t, size_t> residualIndexOf;
    for (size_t i = 0; i < cuts.size(); ++i) {
        residualIndexOf[cuts[i].node] = i;
    }
    const std::string base = "split_of_" + std::string(function->name());
    rt::GraphDraftGroup group(context, program);
    const GraphId forwardId  = group.reserve(base + "_forward", base + "_forward");
    const GraphId backwardId = group.reserve(base + "_backward", base + "_backward");
    {
        // The forward half: the value parameters in, (the value the arm yields, the residuals) out.
        auto &draft = group.transaction(forwardId).edit();
        draft.setFuncType(branchHalves.forward);
        Half forward(draft, function, program, false);
        for (size_t i = 0; i < sourcePorts.size(); i += 2) {
            Type *type               = function->funcType()->normTypeAt(i);
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            forward.portOf[i] = port;
        }
        // Everything that crosses the cut is made *before* the branch is built, in the region the branch
// stands in. A residual the walk projects while an arm is open would be created inside that arm's
// region and then reused by the other arm -- a node of two arms, which the branch-arm invariant
// refuses (`it would run only when ...`). So the residuals are projected here, in region 0.
        for (const Residual &one : cuts) {
            if (!forward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=forward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
        }
        gc_node_ref_t join = rt::kInvalidNodeRef;
        if (!buildHalfBranch(draft, forward, forward.copyOf[condition], arms, false, valueType,
                             built, join)) {
            os << "REINTERP split function=" << function->name()
               << " skip=forward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        std::vector<gc_node_ref_t> residualMembers;
        residualMembers.reserve(cuts.size());
        for (const Residual &one : cuts) {
            gc_node_ref_t value = forward.copyOf[one.node];
            if (value == rt::kInvalidNodeRef && one.parameter != Residual::kNotAParameter) {
                // A value parameter the arms did not happen to read still crosses: the half's port for
                // it is the value itself, which is what a residual is for this kind of member.
                const size_t index = one.parameter * 2;
                if (index < forward.portOf.size()) {
                    value = forward.portOf[index];
                }
            }
            if (value == rt::kInvalidNodeRef) {
                os << "REINTERP split function=" << function->name()
                   << " skip=residual (the forward half does not compute a value the cut carries)\n";
                return built;
            }
            residualMembers.push_back(value);
        }
        const gc_node_ref_t residualValue = fillTuple(
            draft, branchHalves.backward->normTypeAt(0),
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{join, residualValue};
        const gc_node_ref_t out = fillTuple(
            draft, branchHalves.forward->exitType(),
            std::span<const gc_node_ref_t>(exitHalves.data(), exitHalves.size()));
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the residuals and the seeds in, the tangent the picked arm yields out.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(branchHalves.backward);
        Half backward(draft, function, program, true, &cuts, &residualIndexOf);
        Type *residualsType = branchHalves.backward->normTypeAt(0);
        const gc_node_ref_t residualsPort =
            draft.addPortNode(residualsType, draft.allocateRuntimeSlot(residualsType));
        draft.appendNormPort(residualsPort);
        backward.residualsPort = residualsPort;
        for (size_t i = 1; i < sourcePorts.size(); i += 2) {
            Type *type               = function->funcType()->normTypeAt(i);
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            backward.portOf[i] = port;
        }
        // The residuals, projected in region 0 for the same reason as the forward half: an accessor made
        // inside an arm would belong to that arm and be reused by the other one.
        for (const Residual &one : cuts) {
            if (!backward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=backward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
        }
        gc_node_ref_t join = rt::kInvalidNodeRef;
        if (!buildHalfBranch(draft, backward, backward.copyOf[condition], arms, true, tangentType,
                             built, join)) {
            os << "REINTERP split function=" << function->name()
               << " skip=backward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        draft.setExitNode(join);
        draft.setOutputNode(join);
        draft.setReturnNode(join, rt::GCReturnKind::Self);
    }
    group.encode();
    const rt::GCGraph *forwardMade  = group.encoded(forwardId);
    const rt::GCGraph *backwardMade = group.encoded(backwardId);
    built.residuals                 = cuts.size();
    os << "REINTERP split function=" << function->name() << " forward=" << rt::toValue(forwardId)
       << " backward=" << rt::toValue(backwardId)
       << " type=" << (forwardMade != nullptr && forwardMade->funcType() != nullptr
                           ? forwardMade->funcType()->toString()
                           : std::string("-"))
       << " residuals=" << built.residuals << " arms=" << arms.size();
    if (function->recomputesOnBackward()) {
        os << " recompute=1";
    }
    os << '\n';
    built.program  = group.program();
    built.forward  = const_cast<rt::GCGraph *>(forwardMade);
    built.backward = const_cast<rt::GCGraph *>(backwardMade);
    return built;
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
    const std::vector<Residual> graphResiduals = residualsOf(*function, program);
    {
        std::string ports;
        const std::span<const gc_node_ref_t> listed = function->normPorts();
        for (size_t i = 0; i < listed.size(); ++i) {
            ports += (i == 0 ? "" : ",") + std::to_string(listed[i]);
        }
        std::string res;
        for (const Residual &one : graphResiduals) {
            res += (res.empty() ? "" : ",") + std::to_string(one.node);
        }
    }
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
        // The pair does not have to be a tuple fill. A branch answers with its Join, and `jvp` gives
        // that Join the lifted type, so the exit of `jvp(branch body)` is a Join whose slots hold the
        // pair: one arm writes (value, tangent) into it and the other writes its own. There is a pair
        // to cut in that graph all the same -- the author's rule for it is design 4.2: the condition
        // itself is a residual, and the backward half picks the same arm by evaluating that condition.
        // What `split` cannot do yet is step *inside* an arm: an arm is a region, not a data input, so
        // `Half`'s walk (which follows `dataInputsOf`) does not reach it. Saying that is the point of
        // this reading -- a `skip=exit` that does not say which exit leaves the reader with nothing.
        if (exitNode->kind == rt::GCNodeKind::Join && function->funcType() != nullptr &&
            exitNode->dataType() == function->funcType()->exitType()) {
            const gc_node_ref_t condition = branchConditionOf(function, exit);
            SplitGraph refusal;
            if (condition != rt::kInvalidNodeRef) {
                // The first step of the cut a branch needs: enter each arm and say what is in it. The walk a half
                // does follows `dataInputsOf` and never reaches an arm, so this is the entrance it does not
                // have yet -- and the numbers are what the next step builds on (an arm is entered through
                // its form ports, and it is not empty). `enterArmRegions` is the entrance itself: false
                // leaves the reading exactly as it was before this step, which is how the two are compared.
                constexpr bool enterArmRegions = true;
                const gc_node_ref_t branch = branchNodeOf(function, exit);
                if (enterArmRegions) {
                    for (uint32_t region = 1; region < function->regionCount(); ++region) {
                        const rt::GCRegion *one = function->region(region);
                        if (one == nullptr || one->kind != rt::GCRegionKind::Arm ||
                            one->owner != branch) {
                            continue;
                        }
                        const RegionWalk walked = walkRegion(function, region);
                        os << "WALK half=backward region=" << region << " arm=" << walked.arm
                           << " branch=" << branch << " members=" << walked.members
                           << " ports=" << walked.ports << " nested=" << walked.nestedBrchs
                           << " outside=" << walked.outside << '\n';
                    }
                    // The second step: what each arm *yields*, and the Join the backward half makes for
                    // it. An arm ends in the Fill that writes the pair into the Join's slot, so its two
                    // members are the value and the tangent -- the tangent is what the backward half
                    // keeps, and it is read off the Fill's with inputs, never guessed from the arm's
                    // shape. The Join and the region are made for real (a member of the group's own
                    // draft) and the numbers below are their refs; nothing is encoded, because the cut
                    // still refuses, so the step is a construction that can be checked and not a graph
                    // that pretends to run.
                    const rt::GCNode *pairNode = exitNode;
                    Type *pairType             = pairNode->dataType();
                    // The construction is made in a draft of its own, which is dropped without being
                    // encoded: the cut still refuses, so these refs are a construction that can be
                    // checked, not a graph that pretends to run.
                    rt::GraphDraftGroup probe(context, program);
                    const GraphId probeId =
                        probe.reserve("split_arm_probe", "split_arm_probe");
                    auto &halfDraft = probe.transaction(probeId).edit();
                    // The A/B of this step: with `copyArmBodies` false the probe holds an empty region
                    // and an unwired Join per arm, which is the reading before this step; true copies
                    // the arm's body in and joins that arm's half of the pair.
                    constexpr bool copyArmBodies = true;
                    rt::DraftGraphCloneResult copied{};
                    if (copyArmBodies) {
                        auto &txn = probe.transaction(probeId);
                        copied = rt::cloneRuntimeGraphIntoDraft(txn, function, 0);
                    }
                    // What the copy holds for a source node. The clone brings the regions with it, so
                    // an arm's members are the arm's own -- the probe's arm region has the same nodes
                    // the source's has, and the values its members read from outside are values the
                    // probe can name.
                    auto clonedOf = [&](gc_node_ref_t ref) -> gc_node_ref_t {
                        if (!copyArmBodies || ref >= copied.sourceToCloned.size()) {
                            return rt::kInvalidNodeRef;
                        }
                        return copied.sourceToCloned[ref];
                    };
                    const uint32_t conditionRegion =
                        condition < function->nodeCount() ? function->nodeRegion(condition) : 0;
                    for (uint32_t region = 1; region < function->regionCount(); ++region) {
                        const rt::GCRegion *one = function->region(region);
                        if (one == nullptr || one->kind != rt::GCRegionKind::Arm ||
                            one->owner != branch) {
                            continue;
                        }
                        gc_node_ref_t yield = rt::kInvalidNodeRef;
                        for (gc_node_ref_t r = 0; r < function->nodeCount(); ++r) {
                            const rt::GCNode *n = function->node(r);
                            if (n == nullptr || function->nodeRegion(r) != region ||
                                n->kind != rt::GCNodeKind::Fill || n->dataType() != pairType) {
                                continue;
                            }
                            yield = r;
                        }
                        const std::span<const gc_node_ref_t> members =
                            yield == rt::kInvalidNodeRef ? std::span<const gc_node_ref_t>()
                                                         : function->withInputsOf(yield);
                        const gc_node_ref_t tangent =
                            members.size() == 2 ? members[1] : rt::kInvalidNodeRef;
                        // What the copy holds for this arm: the region it landed in, how many members
                        // it has there, and the values those members read from *outside* it -- the
                        // question a cut answers before it can carry anything across.
                        uint32_t probeRegion = 0;
                        size_t probeMembers  = 0;
                        std::vector<gc_node_ref_t> fromOutside;
                        if (!copyArmBodies) {
                            probeRegion = halfDraft.addRegion(
                                rt::GCRegionKind::Arm, 0, rt::kInvalidNodeRef, one->index);
                        }
                        for (gc_node_ref_t r = 0; r < function->nodeCount(); ++r) {
                            if (function->nodeRegion(r) != region) {
                                continue;
                            }
                            const gc_node_ref_t mine = clonedOf(r);
                            if (mine == rt::kInvalidNodeRef) {
                                continue;
                            }
                            ++probeMembers;
                            probeRegion = halfDraft.nodeRegion(mine);
                            for (gc_node_ref_t input : dataInputsOf(function, r)) {
                                const rt::GCNode *read = function->node(input);
                                if (read == nullptr || read->kind == rt::GCNodeKind::Data) {
                                    continue; // a constant is not a value the cut has to carry
                                }
                                const gc_node_ref_t there = clonedOf(input);
                                if (there == rt::kInvalidNodeRef || probeRegion == 0) {
                                    continue;
                                }
                                if (halfDraft.nodeRegion(there) == probeRegion) {
                                    continue;
                                }
                                if (std::find(fromOutside.begin(), fromOutside.end(), there) ==
                                    fromOutside.end()) {
                                    fromOutside.push_back(there);
                                }
                            }
                        }
                        Type *tangentType =
                            tangent != rt::kInvalidNodeRef && function->node(tangent) != nullptr
                                ? function->node(tangent)->dataType()
                                : pairType;
                        const gc_node_ref_t madeJoin =
                            halfDraft.addJoinNode(tangentType, rt::kInvalidNodeRef, 1);
                        const gc_node_ref_t wiredTangent = clonedOf(tangent);
                        size_t wired                     = 0;
                        if (wiredTangent != rt::kInvalidNodeRef) {
                            const std::array<gc_node_ref_t, 1> held{wiredTangent};
                            halfDraft.setWithInputs(
                                madeJoin, std::span<const gc_node_ref_t>(held.data(), held.size()));
                            wired = 1;
                        }
                        os << "ARM half=backward arm=" << one->index << " region=" << region
                           << " yield=" << yield << " members=" << members.size() << " value="
                           << (members.size() == 2 ? members[0] : rt::kInvalidNodeRef) << " tangent="
                           << tangent << " probeRegion=" << probeRegion;
                        if (!copyArmBodies) {
                            os << " probeJoin=" << madeJoin << '\n';
                            continue;
                        }
                        os << " probeMembers=" << probeMembers << " probeJoin=" << madeJoin
                           << " wired=" << wired << " inputs=" << fromOutside.size() << " from=[";
                        for (gc_node_ref_t ref : fromOutside) {
                            // Named by what it is in the source graph, not by the probe's ref: a
                            // parameter is `p<index>` (the two the lifted graph takes are the value
                            // and the seed), anything else `n<ref>`.
                            const rt::GCNode *read = function->node(ref);
                            const std::span<const gc_node_ref_t> ports = function->normPorts();
                            const auto found = std::find(ports.begin(), ports.end(), ref);
                            if (read != nullptr && read->kind == rt::GCNodeKind::Port &&
                                found != ports.end()) {
                                os << 'p' << std::distance(ports.begin(), found) << ' ';
                            } else {
                                os << 'n' << ref << ' ';
                            }
                        }
                        os << "] condition=" << condition << " conditionRegion=" << conditionRegion
                           << " conditionCopied=" << clonedOf(condition) << '\n';
                    }
                }
                // The A/B of this step: with `cutBranchArm` false the cut stops at the reading above
                // and refuses, which is where it stood before the condition was allowed to pick an arm.
                constexpr bool cutBranchArm = true;
                if (cutBranchArm) {
                    std::vector<ArmCut> arms;
                    for (uint32_t region = 1; region < function->regionCount(); ++region) {
                        const rt::GCRegion *one = function->region(region);
                        if (one == nullptr || one->kind != rt::GCRegionKind::Arm ||
                            one->owner != branch) {
                            continue;
                        }
                        const std::span<const gc_node_ref_t> results =
                            one->result < function->nodeCount() ? function->withInputsOf(one->result)
                                                                : std::span<const gc_node_ref_t>();
                        ArmCut arm;
                        arm.region = region;
                        arm.index  = one->index;
                        if (results.size() == 2) {
                            arm.value   = results[0];
                            arm.tangent = results[1];
                        }
                        arms.push_back(arm);
                    }
                    if (arms.size() >= 2) {
                        return cutBranchGraph(context, program, function, os, condition, arms);
                    }
                }
                refuseAt(
                    refusal, function, exit, "the exit",
                    "this graph's exit is the Join of a branch (the arm the condition picks writes the "
                    "pair), so the cut has to happen inside the arms, with the condition carried as a "
                    "residual -- the backward half then picks the arm the forward half took by reading "
                    "the same condition. Each arm is entered and described (`WALK`), its yield is read "
                    "off the Fill that writes the pair (`ARM`), and the backward half copies the arm's "
                    "body into its own region and joins that arm's half of the pair -- but the arm is "
                    "not *chosen* yet: the condition picks it, and reading that condition to pick is "
                    "the next cut, not this one");
                os << "REINTERP split function=" << function->name() << " skip=branch exit=Join"
                   << " condition=" << condition << " why=" << refusal.refusedWhy << '\n';
                return refusal;
            }
            refuseAt(
                refusal, function, exit, "the exit",
                "this graph's exit is a Join that holds the pair and no branch names it, so the two "
                "halves' values meet at a loop -- a loop's residual is what one turn hands the next "
                "(design 4.2), and a half's walk does not enter a region yet");
            os << "REINTERP split function=" << function->name() << " skip=loop exit=Join"
               << " why=" << refusal.refusedWhy << '\n';
            return refusal;
        }
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
        Half forward(draft, function, program, false);
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
        Half backward(draft, function, program, true, &graphResiduals, &residualIndexOf);
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
