// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "split.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/array.h"
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
using camel::core::type::ArrayType;
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
            // A ref that is not a node of this graph is not in any region, and the region query no
            // longer answers the body for it: ask "is this a node" here (`containsNodeRef`), which
            // is the question, instead of leaning on a region query's fallback for a node that is
            // not there.
            if (graph->containsNodeRef(input) && graph->nodeRegion(input) != regionId) {
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

/// `Lift<T>`, read from types: the lifted parameter list is the original's, values first, and then
/// one tangent slot per parameter in the same order (`jvpSignature`, author 2026-11-03), so the value
/// ports are simply the first half. A slot for a parameter with no tangent space (`n: int`) is `()`
/// and stands in the tangent half where its value stands in the value half -- the two halves keep
/// their places position by position, which is the whole reason the slot exists.
std::vector<size_t> valuePortsOf(const camel::core::type::FunctionType *liftedType) {
    std::vector<size_t> values;
    if (liftedType == nullptr) {
        return values;
    }
    const size_t half = liftedType->normTypesCount() / 2;
    values.reserve(half);
    for (size_t i = 0; i < half; ++i) {
        values.push_back(i);
    }
    return values;
}

/// Which parameter a lifted port belongs to, and whether it is that parameter's tangent. A port that
/// is no parameter's (a seeded value, a residual) answers `npos` and false.
///
/// One mapping, written once: the lift's parameters are `count / 2` values followed by `count / 2`
/// tangents, so port `k < half` is parameter `k`'s value and port `half + k` is its tangent. Every
/// reader of that column walks the same list, and the halves are the same length by construction
/// (a `()` slot for a parameter without a tangent space), so no reader has to skip anything.
std::pair<size_t, bool> parameterOfPort(
    const camel::core::type::FunctionType *liftedType, size_t port) {
    constexpr size_t kNone = static_cast<size_t>(-1);
    if (liftedType == nullptr) {
        return {kNone, false};
    }
    const size_t count = liftedType->normTypesCount();
    const size_t half  = count / 2;
    if (port < half) {
        return {port, false};
    }
    if (port < count) {
        return {port - half, true};
    }
    return {kNone, false};
}

/// The graph a call node names, or null when it names none (or the program does not have it).
const rt::GCGraph *calleeGraphOf(
    const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    if (program == nullptr) {
        return nullptr;
    }
    const GraphId callee = graph.calleeIdOf(ref);
    return rt::isValid(callee) ? program->graph(callee) : nullptr;
}

/// Whether the call `ref` names a function that answers the pair (`answersAPair`). This is the one
/// thing the cut needs to know about a callee, and it is a question about **content**, not about where
/// the graph came from: a call to such a callee is answered, on the value half, by `split`'s forward
/// half of that callee (author, 2026-11-03 17:55).
bool answersAPairAt(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    if (ref >= graph.nodeCount() || graph.node(ref) == nullptr ||
        graph.node(ref)->kind != rt::GCNodeKind::Func) {
        return false;
    }
    return answersAPair(calleeGraphOf(graph, program, ref));
}

/// The tangent arguments a call passes: the second half of its **ordinary** arguments, the halves
/// being the values and then their tangents (`jvpSignature`). The `with` arguments stand after both
/// halves (`dataInputsOf` hands back the ordinary ones first), so the list to halve is the ordinary
/// one, not the whole data list.
///
/// One place of that second half is not a tangent anybody takes. A parameter with no tangent space
/// takes a `()` slot in the lifted signature (`Tangent<int> = ()`), the slot holds the empty tuple,
/// and the half that *reads* tangents declares **no port** for it: `buildSplitGraph` gives the
/// backward half one port per tangent that has a space and skips the rest. So the slot's argument is
/// dropped here, by the callee's own signature -- the same list, read from the same place
/// (`parameterOfPort`), so a call site and the graph it calls number their ports alike.
///
/// Handing it on was one argument too many, and the run time says so by name (measured 2026-11-04):
/// `Runtime indirect call norm-arity mismatch: caller graph 'split_of_jvp_of_g_backward' node 6 has
/// 2 args, target graph 'split_of_jvp_of_widen_backward' has 1 norm ports`, on a callee
/// `widen(n: int, x: float)` whose lifted list is `(int, float, (), float)`.
std::vector<gc_node_ref_t> tangentArgumentsOf(
    const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    const std::vector<gc_node_ref_t> all = dataInputsOf(&graph, ref);
    const size_t withCount               = graph.withInputsOf(ref).size();
    const size_t ordinary                = all.size() > withCount ? all.size() - withCount : 0;
    const size_t half                    = ordinary / 2;
    const rt::GCGraph *callee            = calleeGraphOf(graph, program, ref);
    const FunctionType *calleeType       = callee != nullptr ? callee->funcType() : nullptr;
    std::vector<gc_node_ref_t> tangents;
    tangents.reserve(half);
    for (size_t i = half; i < ordinary; ++i) {
        if (calleeType != nullptr && i < calleeType->normTypesCount()) {
            const Type *tangentType = calleeType->normTypeAt(i);
            if (tangentType == nullptr || !camel::core::tangentSpaceOf(tangentType).hasSpace()) {
                continue; // the `()` slot of a parameter with no tangent space
            }
        }
        tangents.push_back(all[i]);
    }
    return tangents;
}

/// The call whose pair's **tangent** component `ref` reads, when `ref` is that read: an access node of
/// the tuple kind taking component 1 of a call to a function that answers a pair. Component 0 is not
/// one of these -- the value the call computes is a value like any other, and the forward half has it
/// as the first component of what its own call answered.
gc_node_ref_t pairTangentReadOf(
    const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    if (ref >= graph.nodeCount() || graph.node(ref) == nullptr ||
        graph.node(ref)->kind != rt::GCNodeKind::Accs) {
        return rt::kInvalidNodeRef;
    }
    const auto *body = graph.nodeBodyAs<rt::GCAccsBody>(ref);
    if (body == nullptr || body->accsKind != rt::GCAccsKind::TupleIndex || body->value != 1) {
        return rt::kInvalidNodeRef;
    }
    const std::vector<gc_node_ref_t> reads = dataInputsOf(&graph, ref);
    if (reads.empty() || !answersAPairAt(graph, program, reads.front())) {
        return rt::kInvalidNodeRef;
    }
    return reads.front();
}

/// The cut a callee already has in this build: the graph its forward half is, and the type that half
/// answers. Both are reserved before either member is built, so a body that calls itself -- or two
/// functions that call each other -- names the graph it is being built into. The table lives only for
/// the length of one transform call: it is not part of any graph and it is not in the core.
struct CutMember {
    GraphId forwardId = rt::kInvalidGraphId;
    Type *answer      = nullptr;
};

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
    /// A `with` parameter of the source graph -> this half's port for it. A `with` value is not a
    /// component of a pair and not a residual: it is a value the caller supplies to the call, and
    /// the half reads it the way the lifted body did, through a port of its own (see
    /// `addWithPorts`). Kept beside `portIndex` rather than in it, because that map is the ordinary
    /// parameters' -- the pair positions `valuePortsOf` and `parameterOfPort` number.
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> withPortOf;
    std::vector<gc_node_ref_t> copyOf;
    std::vector<char> seen;
    /// Which component of every parameter this half takes: false the value, true the tangent.
    bool tangent = false;
    /// The backward half's residual captures: one closure port per residual, in the order
    /// `residualsOf` numbered them. A residual travels in the closure tuple of the `lin` the forward
    /// half builds (author, 2026-11-02 08:30), not in a parameter of this half -- which is what keeps
    /// this half an ordinary function type and the residual count out of every signature.
    std::vector<gc_node_ref_t> closurePorts;
    /// What crosses the cut, when this half is the backward one: a source node the forward half
    /// computes and this half reads is *not* rebuilt here -- it enters by the closure port of its
    /// place in the list. Nothing is recomputed on this side of the cut.
    const std::vector<Residual> *residuals        = nullptr;
    const std::unordered_map<gc_node_ref_t, size_t> *residualIndexOf = nullptr;
    /// The cuts this build already made, keyed by the graph they cut; null when this half is built
    /// outside one -- a branch or a loop body builds its members one at a time, and a call to a
    /// pair-answering callee inside one is refused by name rather than answered with a graph that does
    /// not exist. The value half answers such a call with the callee's **forward half**, so it needs
    /// that graph's id and its answer type before either is built.
    const std::unordered_map<const rt::GCGraph *, CutMember> *calleeCuts = nullptr;

    Half(
        rt::GraphDraft &memberDraft, const rt::GCGraph *sourceGraph, const rt::Program *programGraph,
        bool tangentHalf, const std::vector<Residual> *crossing = nullptr,
        const std::unordered_map<gc_node_ref_t, size_t> *crossingIndexOf = nullptr,
        const std::unordered_map<const rt::GCGraph *, CutMember> *cuts = nullptr)
        : draft(memberDraft), source(sourceGraph), program(programGraph), tangent(tangentHalf),
          residuals(crossing), residualIndexOf(crossingIndexOf), calleeCuts(cuts) {
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

    /// The source inputs this half takes from `ref`: a call to a function that answers a pair takes the
    /// values its caller passes, which are the lifted signature's value ports (read from its types); on
    /// the backward half, the read of such a call's tangent component takes the call's tangent arguments,
    /// because the value it stands for is `lin_g` applied to them. Every other node takes all of them,
    /// in order.
    std::vector<gc_node_ref_t> halfInputsOf(gc_node_ref_t ref) const {
        const std::vector<gc_node_ref_t> all = dataInputsOf(source, ref);
        if (tangent) {
            const gc_node_ref_t call = pairTangentReadOf(*source, program, ref);
            return call != rt::kInvalidNodeRef ? tangentArgumentsOf(*source, program, call) : all;
        }
        if (!answersAPairAt(*source, program, ref)) {
            return all;
        }
        const rt::GCGraph *callee = calleeOf(ref);
        const std::vector<size_t> valuePorts =
            valuePortsOf(callee != nullptr ? callee->funcType() : source->funcType());
        std::vector<gc_node_ref_t> values;
        values.reserve(valuePorts.size());
        for (size_t port : valuePorts) {
            if (port < all.size()) {
                values.push_back(all[port]);
            }
        }
        // ...and the call's `with` arguments, which `valuePortsOf` does not name: they are not
        // ordinary parameters of the lifted signature, they stand at the end of the data list
        // (`dataInputsOf`), and the forward half still passes them on. A half that takes
        // the value ports and not these places the last value it did take in the `with` slot, which
        // is a call the runtime refuses by name (measured 2026-11-04: `FUNC node 2 passing 0
        // arguments and 1 'with' arguments to 'weighted', which takes 1 and 1`).
        for (gc_node_ref_t input : source->withInputsOf(ref)) {
            values.push_back(input);
        }
        return values;
    }

    /// The port residual `index` enters by: the closure port this half declared for it. The values are
    /// in the closure tuple of the `lin` the forward half builds, in `residualsOf`' order, so a read
    /// of one is the port itself -- no projection node, and no count in any type.
    gc_node_ref_t projectResidual(size_t index, SplitGraph &refusal) {
        if (index >= closurePorts.size()) {
            refuseAt(
                refusal, source, rt::kInvalidNodeRef, "a residual",
                "the backward half reads a value of the forward half and this build gave it no "
                "residual capture to read it from");
            return rt::kInvalidNodeRef;
        }
        return closurePorts[index];
    }

    /// Whether `ref` is a value that crosses the cut, so this half takes it from the tuple.
    bool crossesCut(gc_node_ref_t ref) const {
        return residualIndexOf != nullptr && residualIndexOf->find(ref) != residualIndexOf->end();
    }

    /// Pre-seeds the copy of a source node: this half already knows what `ref` is on its side of the
    /// cut -- a loop region's port, or a value the tape hands the round -- so the walk must neither
    /// copy it again nor follow what computes it. `seen` is set with `copyOf`, because the walk asks
    /// `seen` before it asks `copyOf`.
    void seed(gc_node_ref_t ref, gc_node_ref_t target) {
        if (ref >= copyOf.size() || target == rt::kInvalidNodeRef) {
            return;
        }
        copyOf[ref] = target;
        seen[ref]   = 1;
    }

    /// The copy of a source constant, or `kInvalidNodeRef` when `ref` is not a constant or the slot
    /// of one is not readable. A constant has no producer on either side of the cut, so a half that
    /// reads one writes the value the source holds rather than taking it from the tuple (vmap's
    /// `liftConstant` does the same for its constants). This is the one writer: the reachability walk
    /// and the DATA case both write a constant through it, so the two cannot disagree about how a
    /// constant becomes a node of a half.
    gc_node_ref_t writeConstant(gc_node_ref_t ref) {
        if (ref >= source->nodeCount()) {
            return rt::kInvalidNodeRef;
        }
        const rt::GCNode *node = source->node(ref);
        if (node == nullptr || node->kind != rt::GCNodeKind::Data) {
            return rt::kInvalidNodeRef;
        }
        const std::span<const slot_t> slots = source->staticSlots();
        const std::ptrdiff_t index         = -static_cast<std::ptrdiff_t>(node->dataIndex);
        if (node->dataIndex >= 0 || index < 0 || static_cast<size_t>(index) >= slots.size()) {
            return rt::kInvalidNodeRef;
        }
        return draft.materializeStaticValue(slots[static_cast<size_t>(index)], node->dataType());
    }

    /// Places a call's arguments on its copy: the ordinary ones as the call's norm inputs and the
    /// context ones as its `with` inputs. `halfInputsOf` is `dataInputsOf`, which is the ordinary
    /// inputs followed by the context ones (`dataInputsOf` above), so a copy that hands that one list
    /// to `setNormInputs` passes the call's `with` argument as an ordinary one -- and a call the graph
    /// names is checked against what the callee declares at run time. Measured 2026-11-04 on the
    /// composition `vjp((ww) => weighted<ww>(2.0))(3.0)`: `Runtime graph
    /// 'split_of_jvp_of___g_247___forward' has FUNC node 2 passing 1 arguments and 0 'with' arguments
    /// to 'weighted', which takes 1 and 1`.
    void placeCallArguments(gc_node_ref_t made, gc_node_ref_t ref,
                            const std::vector<gc_node_ref_t> &placed) {
        size_t withCount = source->withInputsOf(ref).size();
        if (withCount > placed.size()) {
            withCount = placed.size();
        }
        const size_t normCount = placed.size() - withCount;
        draft.setNormInputs(made, std::span<const gc_node_ref_t>(placed.data(), normCount));
        draft.setWithInputs(made,
                            std::span<const gc_node_ref_t>(placed.data() + normCount, withCount));
    }

    /// The copy of one source node, its inputs already copied. The kinds below are the tier the
    /// plumbing copies; every other kind is refused at the node.
    bool makeCopy(gc_node_ref_t ref, SplitGraph &refusal) {
        if (crossesCut(ref)) {
            const size_t at = residualIndexOf->at(ref);
            if (residuals != nullptr && at < residuals->size() && (*residuals)[at].linearHalf) {
                // The call itself is the residual, not a value of it: what crosses the cut is `lin_g`,
                // which the forward half read as the second component of what its call answered. A
                // reader that wants the pair whole wants something neither half has.
                return refuseAt(
                    refusal, source, ref, "the pair a call answers",
                    "the backward half reads the pair itself, and what crosses the cut is only the "
                    "linear half of it -- the second half of a call reads `lin` and calls it on the "
                    "tangents, it does not read the value and the tangent together");
            }
            const gc_node_ref_t accs = projectResidual(at, refusal);
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
        // A branch and a loop are refused here, before their inputs are asked for. A loop's inputs
        // are the body's yield -- a node of another region -- and the round counter it writes into a
        // port that is nobody's parameter, so asking for them first answers "an operand of the node
        // is not reachable from the half's exit" (or, deeper, "a port") about a graph whose answer is
        // "a loop". The answer is the same either way; only the sentence is not (measured: jvp of a
        // loop of two states).
        switch (node->kind) {
        case rt::GCNodeKind::Brch:
        case rt::GCNodeKind::Join:
            return refuseAt(refusal, source, ref, "a branch", "the plumbing copies straight-line bodies");
        case rt::GCNodeKind::Loop:
            // A loop the cut can copy is one the exit reads through its two projections: `exitShapeOf`
            // answers that shape, records the pair of `TupleIndex` reads and seeds each half with its
            // own value for the component it reads, so the walk stops at the projection and the cell is
            // never entered. A loop reached here is on the chain the exit reads without that pairing --
            // read whole, or from one side only -- and a loop cell is not a value: its inputs are the
            // body's yield, a node of another region, and the round counter it writes into a port that
            // is nobody's parameter. Copying it means copying a body region.
            return refuseAt(
                refusal, source, ref, "a loop",
                "this loop is on the chain the exit reads and neither half has a value for it: the cut "
                "copies a loop where the exit reads its two components through one projection each, and "
                "here the half would have to copy a body region");
        default:
            break;
        }
        // Every input must be available: either copied already, or one of the values that cross the
        // cut, which this half takes out of the residual tuple at its place in the list. A graph the
        // walk could not finish (a cycle, a reference that is not a node) is a refusal, not a node with
        // a hole in its inputs.
        std::vector<gc_node_ref_t> inputs;
        for (gc_node_ref_t input : halfInputsOf(ref)) {
            // A constant has no producer, so either half can write it again: a half that reads one
            // takes it as available rather than as an operand that failed to arrive. Without this the
            // answer would turn on where a literal sits and not on what the half computes -- a constant
            // on a value operand is a value like any other, so a body whose only peculiarity is a bare
            // constant there cuts like any other body. A constant whose slot this build cannot read is
            // not written here, and the operand then fails to arrive.
            if (input < source->nodeCount() && copyOf[input] == rt::kInvalidNodeRef) {
                const gc_node_ref_t written = writeConstant(input);
                if (written != rt::kInvalidNodeRef) {
                    copyOf[input] = written;
                }
            }
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
            // A `with` parameter is a value the caller supplies, not a component of a pair: this
            // half made a port of its own for it, and the read is that port.
            const auto with = withPortOf.find(ref);
            if (with != withPortOf.end()) {
                copyOf[ref] = with->second;
                return true;
            }
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
                    "the value half reads the tangent of parameter " +
                        std::to_string(parameterOfPort(source->funcType(), index).first) +
                        ", which the forward half does not take");
            }
            return refuseAt(
                refusal, source, ref, "a parameter",
                "the backward half reads the value of parameter " +
                        std::to_string(parameterOfPort(source->funcType(), index).first) +
                    ", and it is not one of the values the cut carries");
        }
        // What the refusal above states is the invariant this cut keeps: the tangent half reads only
        // values that cross the cut -- the ones `residualsOf` listed -- plus what the tangent half
        // itself computes from them. A parameter's value is one of the former whenever the tangent
        // half reads it: `residualsOf` asks the tangent side's own walk what it reads, not which
        // parameter index a port belongs to, and it keeps a parameter's value port on the same terms
        // it keeps a computed value (2026-11-02 00:30: the rule does not split by type; 2026-11-03
        // 15:15: 后一半要读的东西都是残差). That is also why the refusal is not reachable today: a
        // parameter value the backward half reads is in the residual list before this walk runs, so
        // `makeCopy` answers it from the tuple (`crossesCut`, above) and never arrives here.
        //
        // It is kept, deliberately, and not replaced by an assertion: 对 > 明确拒绝 > 错数 > 崩 is
        // the order, and an assertion here would turn a refusal into the worst of the four. Its being
        // unreachable in the shapes this lane measured is a reading and not a proof -- measured
        // 2026-11-04: no case in the tree pins this message, and this lane could not construct a
        // shape that still reaches it; another lane instrumented the same site on other shapes and
        // saw zero entries. Zero entries and unreachable are two different conclusions, and this
        // project has already paid once tonight for treating them as one. If it is ever *proved*
        // unreachable for every input, delete this refusal and demote the invariant above to a
        // comment -- do not delete it on the strength of a count. Never delete it while it is the
        // only refusal standing between this walk and a half that reads a value nothing carries.
        case rt::GCNodeKind::Data: {
            // A constant stands for the same value in the copy; `writeConstant` is that writer, so the
            // DATA case and the reachability walk write it the same way. A constant whose slot is not
            // readable has no copy.
            const gc_node_ref_t written = writeConstant(ref);
            if (written == rt::kInvalidNodeRef) {
                return refuseAt(
                    refusal, source, ref, "a constant", "the slot of the constant is not readable");
            }
            copyOf[ref] = written;
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
            if (answersAPairAt(*source, program, ref) && !tangent) {
                // The callee answers the pair (value, tangent) by its content (`answersAPair`), so the
                // value half answers the call with **`split`'s forward half of that callee**: the same
                // values in, `(result, lin)` out. `lin` is the callee's own linear half, closed over
                // whatever its body reads, so the call site never sees those values -- `residualsOf`
                // names `lin` itself for the backward half to call (author, 2026-11-03 17:55).
                const rt::GCGraph *calleeGraph = calleeOf(ref);
                CutMember nested;
                if (calleeCuts != nullptr && calleeGraph != nullptr) {
                    const auto known = calleeCuts->find(calleeGraph);
                    if (known != calleeCuts->end()) {
                        nested = known->second;
                    }
                }
                if (nested.answer == nullptr) {
                    return refuseAt(
                        refusal, source, ref, "a call to a function that answers the pair",
                        "the cut of that callee has not been made here, so the value half has no "
                        "forward half of it to call -- a branch or a loop body builds its members one "
                        "at a time, and this call is inside one");
                }
                const gc_node_ref_t made = draft.addFuncNode(
                    nested.forwardId, nested.answer, 0, draft.allocateRuntimeSlot(nested.answer), 0);
                placeCallArguments(made, ref, inputs);
                draft.addDependency(nested.forwardId);
                copyOf[ref] = made;
                return true;
            }
            // A fact about the call, not about the half it is copied into: when the rule's expansion
            // recorded how many of the call's trailing arguments are tangents, the copy is the same
            // call and says the same thing. It is what `transpose` reads at a call inside an arm.
            // The record is a **count of the call's trailing arguments**, not a mask of positions
            // (`GCNode::tangentArgs`, `GCGraph::callTangentArgsOf`): both readers -- the copy here,
            // and `transpose.cpp`'s `calleeParams - tangentArgs` -- have to take it that way, and a
            // call whose tangents are not its trailing arguments has no record that says so.
            const gc_node_ref_t made = draft.addFuncNode(
                callee, node->dataType(), 0, draft.allocateRuntimeSlot(node->dataType()), 0,
                source->callTangentArgsOf(ref));
            placeCallArguments(made, ref, inputs);
            draft.addDependency(callee);
            copyOf[ref] = made;
            return true;
        }
        case rt::GCNodeKind::Accs: {
            // A component of the pair a call answers. On the value half the call in hand answers
            // `(result, lin)`, so component 0 is the value the source's call computed and component 1
            // is the linear half -- a function value, not the source's tangent. On the backward half a
            // read of component 1 is **the call of that linear half**: one indirect call, on the
            // tangents the source call passed, and the call site never sees what the callee's body
            // reads (author, 2026-11-03 17:55).
            const auto *body = source->nodeBodyAs<rt::GCAccsBody>(ref);
            const std::vector<gc_node_ref_t> reads = dataInputsOf(source, ref);
            if (body == nullptr || reads.empty()) {
                return refuseAt(
                    refusal, source, ref, "an access node",
                    "the node reads no input, so the half has nothing to project");
            }
            // A member of a **struct** is read by name (`qq.a`, `GCAccsKind::StructKey`) and a member
            // of a tuple by place (`qq.0`, `GCAccsKind::TupleIndex`). The draft writes both forms
            // (`addAccsNode(type, key)` and `addAccsNode(type, index)`), and the copy says whichever
            // one the source said: a keyed read copied as a place would name another member, and on a
            // `Struct` it would index the value as if it were a `Tuple` -- `idx < t->size()`, "Tuple
            // index out of bounds" in `nodevm.cpp` (measured 2026-11-04 on
            // `split(jvp((qq: Point) => qq.a * 4.0 + qq.b))(p)`, while the copy dropped the key).
            //
            // The name needs no place in either half's port list. A struct is a port like any other --
            // `Tangent<Point>` is the struct of the members' tangents (design principles 9.9), so the
            // two halves take one `Point` each and read the same member of it by the same name
            // (`jvp.cpp#LiftState` projects a keyed access the same way, with `addAccsNode(type,
            // key)`). What the cut was missing was the name in the copy and not the shape of the
            // ports: `split_struct_field_read` reads `forward y= 11.0` and `lin dp= 4.0`, the two
            // `jvp` alone already answered, and `split_struct_port` -- the same program without the
            // field read -- reads a struct through both halves.
            if (const gc_node_ref_t call = pairTangentReadOf(*source, program, ref);
                call != rt::kInvalidNodeRef) {
                if (!tangent) {
                    // The value half's copy of that call answers `(result, lin)`: its component 1 is a
                    // function, not the tangent the source asked for.
                    return refuseAt(
                        refusal, source, ref, "a tangent of a call",
                        "the value half computes the value of the call, and the tangent of a call is "
                        "computed after the cut -- its component 1 here is the linear half, which is "
                        "a function and not a tangent");
                }
                const auto index = residualIndexOf->find(call);
                if (index == residualIndexOf->end()) {
                    return refuseAt(
                        refusal, source, ref, "a linear half",
                        "the cut does not carry the linear half of this call, so the second half has "
                        "no function to call");
                }
                const gc_node_ref_t lin = projectResidual(index->second, refusal);
                if (lin == rt::kInvalidNodeRef) {
                    return false;
                }
                const gc_node_ref_t made = draft.addCallNode(node->dataType());
                if (!inputs.empty()) {
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs.data(), inputs.size()));
                }
                draft.setWithInputs(made, std::span<const gc_node_ref_t>(&lin, 1));
                copyOf[ref] = made;
                return true;
            }
            const gc_node_ref_t made =
                body->accsKind == rt::GCAccsKind::StructKey
                    ? draft.addAccsNode(node->dataType(), body->key())
                    : draft.addAccsNode(node->dataType(), body->value);
            draft.setNormInputs(made, inputs);
            copyOf[ref] = made;
            return true;
        }
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
                // A branch or a loop is refused at the node, not through what its inputs name. A
                // loop's inputs are the body's yield -- a node of another region -- and the round
                // counter the loop writes into a port that is nobody's parameter, so expanding them
                // copies a body region into a straight-line half and reports the first port of it
                // ("a port, the round number") for a graph whose answer is "a loop". The answer is
                // the same either way; only the sentence is not (measured: `jvp` of a loop of two
                // states, `the port is not a parameter of the graph`).
                const rt::GCNode *here   = source->node(ref);
                const bool refusedAtNode = here != nullptr &&
                                           (here->kind == rt::GCNodeKind::Loop ||
                                            here->kind == rt::GCNodeKind::Brch ||
                                            here->kind == rt::GCNodeKind::Join);
                if (refusedAtNode) {
                    continue;
                }
                for (gc_node_ref_t input : halfInputsOf(ref)) {
                    // A value that crosses the cut is read out of the residual tuple: walking into
                    // what computes it would copy the forward half into this one, which is exactly
                    // the recomputation the cut exists to avoid.
                    if (input >= source->nodeCount() || seen[input] != 0 || crossesCut(input) ||
                        copyOf[input] != rt::kInvalidNodeRef) {
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

/// The backward half's residual captures: one closure port per residual, in the order `residualsOf`
/// numbered them, each of the residual's own type. The order and the count are the graph's answer, so
/// the closure tuple the forward half builds and the ports this declares are read off the same list.
///
/// The draft is a template parameter for the reason the branch helpers give: this file builds through
/// what the transaction hands it, and a parameter written as the type is one more of the direct
/// connections `runtime.graph_draft_ledger` counts, which may only fall.
template <typename Draft>
void addResidualCaptures(Draft &draft, const std::vector<Residual> &residuals, Half &backward) {
    backward.closurePorts.reserve(residuals.size());
    for (const Residual &residual : residuals) {
        if (residual.type == nullptr) {
            continue;
        }
        const gc_node_ref_t port =
            draft.addPortNode(residual.type, draft.allocateRuntimeSlot(residual.type));
        draft.appendClosureNode(port);
        backward.closurePorts.push_back(port);
    }
}

} // namespace

/// `lin`: the backward graph, closed over the residuals the forward half computed.
///
/// A FILL of the `FunctionClosure` kind -- the kind a rule's right side already builds for its
/// closure (`rule_expand.cpp` reads one back): the static input is a function value naming the
/// backward graph, whose closure layout is the one this build read from that graph, and the `with`
/// inputs are the residual values in slot order. This is what "the kernel builds `lin` once it has
/// the graph" means (author, 2026-11-02 08:30): the values do not exist before the forward half runs,
/// so `lin` is built where they do, and what it captures is stated by no signature.
///
/// Here because *two* builds write this shape, not one: the cut hands `lin` out of the forward half,
/// and a call site that transposes a pair-answering callee closes the same graph over the same values
/// (`transpose.cpp`, `transposePairCall`). One writer, so a closure a cut made and a closure a call
/// site made are the same value.
gc_node_ref_t buildLinearClosure(
    rt::GraphDraft &draft, const rt::Program *program, GraphId backwardId,
    const camel::core::type::TupleType *closure, FunctionType *linType,
    std::span<const gc_node_ref_t> residuals) {
    auto *lin = ::Function::create(program, backwardId, closure, mm::autoSpace());
    const gc_node_ref_t source = draft.materializeStaticValue(toSlot<::Function *>(lin), linType);
    std::vector<size_t> slots(residuals.size());
    std::iota(slots.begin(), slots.end(), size_t{0});
    const gc_node_ref_t value =
        draft.addFillNode(linType, rt::makeFillPayload(rt::GCFillKind::FunctionClosure, slots));
    draft.setNormInputs(value, std::span<const gc_node_ref_t>(&source, 1));
    draft.setWithInputs(value, residuals);
    return value;
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
    // The index of a parameter is its place in the graph's port list (`(value, tangent)` pairs), so
    // the value of the k-th parameter is at 2k.
    std::vector<size_t> portIndex(nodeCount, Residual::kNotAParameter);
    const std::span<const gc_node_ref_t> ports = lifted.normPorts();
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i] < nodeCount) {
            portIndex[ports[i]] = i;
        }
    }
    // The `with` ports of the lifted graph: a value the call supplies, read by the body through a
    // port of its own. A `with` port the tangent side reads crosses the cut like any other value it
    // reads -- 后一半要读的东西都是残差，`with` 值也在内 (author, 2026-11-03 15:15) -- so it is a
    // residual, and the backward half reads it out of `lin`'s closure instead of taking a `with` list
    // of its own. Asking it here, and not only through the walk below, is what makes the answer
    // independent of whether the result side happens to read the same weight: a weight only the
    // tangent side reads is still a value that side reads.
    const std::span<const gc_node_ref_t> withPorts = lifted.withPorts();
    const auto isWithPort                          = [&withPorts](gc_node_ref_t ref) {
        return std::find(withPorts.begin(), withPorts.end(), ref) != withPorts.end();
    };
    // A value the tangent side reads and the result side already computed crosses the cut: it is a
    // residual, and the walk stops at it (what is inside the forward half is not this function's
    // business). A node the result side never reaches is part of the tangent chain: the walk goes
    // through it.
    std::vector<char> visited(nodeCount, 0);
    std::vector<gc_node_ref_t> found;
    // The calls whose **linear half** crosses the cut, in the order the walk named them: the call
    // site does not see the residuals of the callee's body (author, 2026-11-03 17:55), it sees `lin_g`.
    std::vector<gc_node_ref_t> linearHalves;
    {
        // Two worklists. `stack` is the walk proper; `later` holds the tangent arguments of such a
        // call, which are computed **after** the cut -- the second half works them out from the
        // tangents it was handed -- even though the result side reaches them (they are inputs of the
        // call it copies). The walk goes through them instead of stopping at them, and it drains the
        // ordinary list first so that a value reached both ways keeps the reading it has today.
        std::vector<gc_node_ref_t> stack;
        std::vector<gc_node_ref_t> later;
        if (tangentHalf < nodeCount) {
            stack.push_back(tangentHalf);
        }
        while (!stack.empty() || !later.empty()) {
            const bool behindCut = stack.empty();
            const gc_node_ref_t ref = behindCut ? later.back() : stack.back();
            if (behindCut) {
                later.pop_back();
            } else {
                stack.pop_back();
            }
            if (ref >= nodeCount || visited[ref] != 0) {
                continue;
            }
            visited[ref] = 1;
            if (!behindCut && isWithPort(ref)) {
                // A `with` port has no producer on either side of the cut, so nothing is computed
                // for it and the walk has nowhere further to go: the value is the call's, and the
                // forward half is the half that holds the port which receives it.
                if (std::find(found.begin(), found.end(), ref) == found.end()) {
                    found.push_back(ref);
                }
                continue;
            }
            // A read of the pair's **tangent** component of a call whose callee answers the pair: what
            // it stands for is that callee's own linear half applied to the tangents the call passes.
            // So the cut carries the half -- the forward half reads it as the second component of what
            // its call answered, the backward half calls it -- and the values inside the callee stay
            // inside it, in its closure.
            if (const gc_node_ref_t call = pairTangentReadOf(lifted, program, ref);
                call != rt::kInvalidNodeRef) {
                const rt::GCGraph *calleeGraph = calleeGraphOf(lifted, program, call);
                Type *linType =
                    calleeGraph != nullptr && calleeGraph->funcType() != nullptr
                        ? splitHalvesOf(calleeGraph->funcType()).backward
                        : nullptr;
                if (linType == nullptr) {
                    // No linear half to name: the call is then only the pair it is, and the branch
                    // below reads it the way it read one before the half existed.
                    stack.push_back(call);
                    continue;
                }
                if (std::find(linearHalves.begin(), linearHalves.end(), call) == linearHalves.end()) {
                    linearHalves.push_back(call);
                }
                // The call's **tangent arguments** go on the late list: they are what the second half works
                // out from the tangents it was handed, so the cut does not carry them. They are walked
                // -- through the call's own value arguments, which the second half reads and which
                // therefore are residuals -- but with the same rule the result side uses, which is why
                // a call there is descended by its value arguments alone.
                for (gc_node_ref_t argument : tangentArgumentsOf(lifted, program, call)) {
                    if (argument < nodeCount && visited[argument] == 0 &&
                        std::find(later.begin(), later.end(), argument) == later.end()) {
                        later.push_back(argument);
                    }
                }
                continue;
            }
            if (behindCut) {
                // A value the second half computes *after* the cut -- one of the tangent arguments of
                // such a call. The walk goes through it to the values it reads, and never carries the
                // node itself: what it reads crosses the cut by the rules below, one step further in,
                // and its own tangents (a rule kernel's, `callTangentArgsOf`) are the second half's
                // arguments rather than values that cross. Walking into those is what made a
                // parameter's tangent arrive as the parameter's value: the second half then read its
                // own argument out of the closure, and `twice(c + y)` was transposed as `3*(c+y)`
                // instead of `3*(dc+dy)` (measured 2026-11-04).
                std::vector<gc_node_ref_t> onward;
                const GraphId lateCalleeId = lifted.calleeIdOf(ref);
                const rt::GCGraph *lateCallee =
                    program != nullptr && rt::isValid(lateCalleeId) ? program->graph(lateCalleeId)
                                                                    : nullptr;
                const std::vector<gc_node_ref_t> arguments   = dataInputsOf(&lifted, ref);
                const size_t withArguments                   = lifted.withInputsOf(ref).size();
                const size_t ordinary =
                    arguments.size() > withArguments ? arguments.size() - withArguments : 0;
                if (answersAPair(lateCallee)) {
                    for (size_t i = 0; i < ordinary / 2; ++i) {
                        onward.push_back(arguments[i]);
                    }
                    for (size_t i = ordinary; i < arguments.size(); ++i) {
                        onward.push_back(arguments[i]);
                    }
                } else {
                    const uint16_t tangents = lifted.callTangentArgsOf(ref);
                    for (size_t i = 0; i < arguments.size(); ++i) {
                        if (i < ordinary && i < rt::kTangentArgumentBits &&
                            (tangents & (uint16_t(1) << i)) != 0) {
                            continue;
                        }
                        onward.push_back(arguments[i]);
                    }
                }
                for (gc_node_ref_t input : onward) {
                    if (input < nodeCount && visited[input] == 0) {
                        stack.push_back(input);
                    }
                }
                continue;
            }
            if (!behindCut && beforeCut[ref] != 0 && ref != tangentHalf) {
                // A call of a callee that answers the pair (value, tangent) reads both sides of the
                // cut and is not itself carried (see the note where the list is filtered below): what
                // the tangent half recomputes the pair from is the values its caller passes, so the
                // walk goes through the call and keeps the value arguments. **Which callees those are
                // is decided by the callee's content** (`answersAPair`, the same criterion the cut
                // itself is built on) -- not by a record of what the callee was lifted from, which
                // would answer differently for a hand-written graph of the same content.
                const GraphId calleeId = lifted.calleeIdOf(ref);
                const rt::GCGraph *calleeGraph =
                    program != nullptr && rt::isValid(calleeId) ? program->graph(calleeId) : nullptr;
                if (!answersAPair(calleeGraph)) {
                    found.push_back(ref);
                    continue;
                }
                const std::vector<gc_node_ref_t> arguments = dataInputsOf(&lifted, ref);
                // The lifted callee takes its values and then its tangents (`jvpSignature`), so the
                // value arguments are the first half of what the call passes -- of the *ordinary*
                // arguments. `dataInputsOf` is the ordinary inputs followed by the context ones
                // (`dataInputsOf` above), so the count to halve is the list without the call's `with`
                // arguments: halving the whole list puts the boundary in the wrong place as soon as a
                // lifted callee keeps a `with` list, and the value that lands on the boundary is the
                // caller-side weight. 后一半要读的东西都是残差 (author, 2026-11-03 15:15), and this
                // half hands the callee that weight, so the walk keeps it; dropping it is what made
                // the composition `vjp((ww) => f<ww>(..))(w)` refuse one step past the exit, in the
                // backward walk's own words, `the backward half reads the value of parameter 0, and it
                // is not one of the values the cut carries` (measured 2026-11-04).
                const size_t withArguments = lifted.withInputsOf(ref).size();
                const size_t valueArguments =
                    arguments.size() > withArguments ? arguments.size() - withArguments : 0;
                for (size_t i = 0; i < valueArguments / 2; ++i) {
                    const gc_node_ref_t input = arguments[i];
                    if (input < nodeCount && visited[input] == 0) {
                        stack.push_back(input);
                    }
                }
                // ...and the context arguments themselves, which are values it reads just as it reads
                // the ordinary ones.
                for (size_t i = valueArguments; i < arguments.size(); ++i) {
                    const gc_node_ref_t input = arguments[i];
                    if (input < nodeCount && visited[input] == 0) {
                        stack.push_back(input);
                    }
                }
                continue;
            }
            if (!behindCut && portIndex[ref] != Residual::kNotAParameter) {
                // A parameter the tangent side reads is a residual too, even though the result side
                // never reached it -- and what goes into the packet is the parameter's *value* port,
                // not its tangent. The forward half is handed "the value of every parameter in,
                // (result, residuals) out": it has a port for every value and none for a tangent, so
                // a value port is the only thing it can hand over. `xs[i]` is the shape that needs
                // it: the transpose of the read writes its cotangent into `zeros_like(xs)`, a `T[]`'s
                // length is not a thing its type carries, and this parameter holds the array.
                const size_t index = portIndex[ref];
                // Which parameter this port belongs to, asked of the one mapping that knows the layout
                // `jvpSignature` writes (`parameterOfPort`) instead of restating it: a port below the
                // half is its own parameter's value and one above it is the tangent whose value stands
                // exactly `half` places earlier.
                const size_t valuePort = parameterOfPort(lifted.funcType(), index).first;
                // A parameter the tangent side reads crosses too: the residual set is what the
                // backward half actually reads, whether the forward half computed it or it is a
                // parameter itself. The ruling of 2026-11-02 00:30: the rule does not split by type.
                if (valuePort < ports.size() && ports[valuePort] < nodeCount &&
                    std::find(found.begin(), found.end(), ports[valuePort]) == found.end()) {
                    found.push_back(ports[valuePort]);
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
    // Program order: the parameters by their index, then the computed values by their node.
    // A graph that says its calls are recomputed on the backward pass (`remat`,
    // kGraphFlagRecomputeBackward, camel/runtime/graph.h) crosses the cut with its parameters and
    // nothing else: every other value is computed again in the backward half from them, because a
    // value that is not a residual is not where Half's walk stops -- it copies what produces it
    // instead (the `crossesCut` branch of copyHalf). The mark says "store less, compute again", and
    // this is the whole of what it buys at the cut: the parameters are what the call is given, and
    // what the body made of them is not carried. The values are the same either way; only the count
    // of what crosses the cut changes, which is why the reading below prints the mark beside it.
    if (lifted.recomputesOnBackward()) {
        std::erase_if(found, [&portIndex, &linearHalves](gc_node_ref_t ref) {
            if (std::find(linearHalves.begin(), linearHalves.end(), ref) != linearHalves.end()) {
                // A linear half is not recomputed here: it is the callee's own second half, and
                // recomputing it would mean rebuilding the callee inside this one.
                return false;
            }
            return portIndex[ref] == Residual::kNotAParameter;
        });
    }
    // A call of a callee that answers the pair (value, tangent) is not a value the cut can carry, and
    // the reason is the same one that makes the call readable at all: the pair. The value half does
    // not read tangents, so it answers with the graph the callee's value component comes from (a call
    // of `f`, not of `jvp_of_f`), which is the result and not the pair; the tangent half rebuilds the
    // pair from the values it is handed. A pair built by one half cannot be what the other half reads,
    // so the pair is not a residual -- the values it is made of are (`x` crosses, and the value the
    // call made crosses; the tangent does not). Without this the forward half would hand a float where
    // the tuple says a pair, which is a type error at run time, not a refusal.
    //
    // The callee is named by its **content** (`answersAPair`), the same criterion the cut is built on:
    // a hand-written graph that answers the pair is treated exactly as a graph lifted by `jvp`.
    if (program != nullptr) {
        std::erase_if(found, [&](gc_node_ref_t ref) {
            if (std::find(linearHalves.begin(), linearHalves.end(), ref) != linearHalves.end()) {
                // A call whose linear half crosses is the one call that *is* carried: what crosses is
                // `lin`, which only exists because the forward half made that call.
                return false;
            }
            const GraphId calleeId = lifted.calleeIdOf(ref);
            const rt::GCGraph *calleeGraph =
                rt::isValid(calleeId) ? program->graph(calleeId) : nullptr;
            return answersAPair(calleeGraph);
        });
    }
    // The linear halves are the remaining members of the list: a computed value like any other, in
    // program order, and after the parameters (they are not parameters).
    for (gc_node_ref_t call : linearHalves) {
        if (std::find(found.begin(), found.end(), call) == found.end()) {
            found.push_back(call);
        }
    }
    // The parameters' values come first, each in its own parameter's slot, because that is the order
    // a reader of the closure goes by: "slot `at` is parameter `at`'s value"
    // (`modules/transforms/transpose.cpp`). The walk can reach one value by two routes -- a
    // parameter's value port seen as a value that crosses the cut, and the same port seen as what a
    // parameter the tangent side reads stands for (`ports[valuePort]` below) -- and while a second
    // copy of a value is not lost information, it must not take a slot that belongs to another
    // parameter. Two programs of one type show why:
    //
    //     sum(x * s)  residuals [x, s, s]     slots 0 and 1 are `x` and `s`
    //     sum(s * x)  residuals [x, x, s]     slot 1 is `x` again, and `s` stands at 2
    //
    // The transposition read slot 1 for `s`, found an array, and its type check refused it -- rightly,
    // an array is not the number `s` holds -- then asked for the value it could not find, which added
    // a norm port no call site fills. So a value that appears more than once is placed after
    // everything that appears once: the copies are kept (the count is the graph's answer, and the
    // reading below prints it), and the slots of the parameters stay their own.
    {
        std::vector<gc_node_ref_t> once;
        std::vector<gc_node_ref_t> again;
        for (const gc_node_ref_t ref : found) {
            if (std::find(once.begin(), once.end(), ref) == once.end()) {
                once.push_back(ref);
            } else {
                again.push_back(ref);
            }
        }
        std::sort(once.begin(), once.end(), [&portIndex](gc_node_ref_t a, gc_node_ref_t b) {
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
        std::sort(again.begin(), again.end(), [&portIndex](gc_node_ref_t a, gc_node_ref_t b) {
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
        std::vector<gc_node_ref_t> ordered;
        ordered.reserve(found.size());
        ordered.insert(ordered.end(), once.begin(), once.end());
        ordered.insert(ordered.end(), again.begin(), again.end());
        found = std::move(ordered);
    }
    residuals.reserve(found.size());
    for (gc_node_ref_t ref : found) {
        Residual residual;
        residual.node = ref;
        if (std::find(linearHalves.begin(), linearHalves.end(), ref) != linearHalves.end()) {
            // The linear half of a call: its type is the callee's own backward half, `(tangents) =>
            // tangent`, and its name is the call because that is what it is the second answer of.
            const rt::GCGraph *calleeGraph = calleeGraphOf(lifted, program, ref);
            residual.parameter  = Residual::kNotAParameter;
            residual.linearHalf = true;
            residual.type       = calleeGraph != nullptr && calleeGraph->funcType() != nullptr
                                      ? splitHalvesOf(calleeGraph->funcType()).backward
                                      : nullptr;
            residuals.push_back(residual);
            continue;
        }
        const size_t index = portIndex[ref];
        if (index != Residual::kNotAParameter) {
            // A parameter's value: its type is the parameter's, and its number is half its index.
            residual.parameter = parameterOfPort(lifted.funcType(), index).first;
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

/// The callees whose own cuts a graph's **value half** needs: the pair-answering calls that half
/// copies, found by the same walk it makes -- all of a node's inputs, and at a call to a function that
/// answers the pair the values the caller passes plus the call's `with` arguments, because that is what
/// the forward half of that callee takes. Reserved before either member of this cut is built, so that a
/// body that calls itself names the graph it is being built into.
std::vector<rt::GCGraph *>
pairCalleesOnValueChain(const rt::GCGraph &graph, const rt::Program *program) {
    std::vector<rt::GCGraph *> callees;
    const gc_node_ref_t exit = ungated(&graph, graph.exitNodeRef());
    if (exit >= graph.nodeCount() || graph.node(exit) == nullptr) {
        return callees;
    }
    const auto &exitWith = graph.withInputsOf(exit);
    if (graph.node(exit)->kind != rt::GCNodeKind::Fill || exitWith.size() != 2) {
        return callees;
    }
    const size_t nodeCount = static_cast<size_t>(graph.nodeCount());
    std::vector<char> visited(nodeCount, 0);
    std::vector<gc_node_ref_t> stack;
    const gc_node_ref_t resultHalf = ungated(&graph, exitWith[0]);
    if (resultHalf >= nodeCount) {
        return callees;
    }
    visited[resultHalf] = 1;
    stack.push_back(resultHalf);
    while (!stack.empty()) {
        const gc_node_ref_t ref = stack.back();
        stack.pop_back();
        std::vector<gc_node_ref_t> inputs;
        if (answersAPairAt(graph, program, ref)) {
            const GraphId calleeId = graph.calleeIdOf(ref);
            if (program != nullptr && rt::isValid(calleeId) && program->graph(calleeId) != nullptr) {
                callees.push_back(program->graph(calleeId));
            }
            const std::vector<gc_node_ref_t> all = dataInputsOf(&graph, ref);
            const size_t withCount               = graph.withInputsOf(ref).size();
            const size_t ordinary                = all.size() > withCount ? all.size() - withCount : 0;
            for (size_t i = 0; i < ordinary / 2 && i < all.size(); ++i) {
                inputs.push_back(all[i]);
            }
            for (size_t i = ordinary; i < all.size(); ++i) {
                inputs.push_back(all[i]);
            }
        } else {
            inputs = dataInputsOf(&graph, ref);
        }
        for (gc_node_ref_t input : inputs) {
            if (input < nodeCount && visited[input] == 0) {
                visited[input] = 1;
                stack.push_back(input);
            }
        }
    }
    return callees;
}

SplitHalves splitHalvesOf(FunctionType *function) { return splitHalvesWith(function, nullptr); }

SplitHalves splitHalvesWith(FunctionType *function, Type *residuals) {
    SplitHalves halves;
    if (function == nullptr) {
        halves.why = "there is no function to split";
        return halves;
    }
    // A `with` parameter is not a reason to refuse: both halves declare it as a port of their own
    // (`addWithPorts`), so the captured value enters each half the way the lifted body read it. It
    // used to be refused here with "a captured value has no port to enter them by", a reason that
    // stopped being true once the closure had capture ports of its own -- and a `jvp` that lifts a
    // weight, which is what design principles 9.9's `vjp = transpose(split(jvp(f)))` needs, can only
    // be cut if `split` takes such a function.
    const size_t count = function->normTypesCount();
    if (count == 0 || count % 2 != 0) {
        halves.why = "`split` takes a lifted function, whose parameters are the value of every "
                     "parameter and then the tangent of every one, so they come in two halves of "
                     "equal length";
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
    // property of the *graph*, so the graph answers it (`residualsOf`), and the tuple it names is `lin`'s
    // **closure** -- the values travel with the function value, not in a parameter of it (author,
    // 2026-11-02 08:30: the residuals are the backward half's capture ports). A caller with no graph
    // (the operator resolver, which sees types only) names none, and then the closure is empty: a
    // signature has no reading to name, and the count a reading states comes from the graph
    // (`residuals=`).
    TupleType *closure = nullptr;
    if (residuals == nullptr) {
        closure = TupleType::create(std::vector<Type *>{});
    } else if (residuals->code() == TypeCode::Tuple) {
        closure = tt::as_ptr<TupleType>(residuals);
    } else {
        closure = TupleType::create(std::vector<Type *>{residuals});
    }

    param_vec_t forwardParams;
    param_vec_t tangentParams;
    // The two halves are the two halves of the list: the values are ports `0 .. n`, the tangents are
    // `n .. 2n` (`jvpSignature`, author 2026-11-03). The forward half takes the values in the
    // parameters' order and the backward half the tangent that stands in the same place -- which is
    // what makes a `()` slot for a parameter with no tangent space mean something rather than
    // swallow its neighbour's tangent.
    const size_t half = count / 2;
    for (size_t i = 0; i < half; ++i) {
        forwardParams.emplace_back(function->normTypeAt(i), false);
        // A parameter with no tangent space stands in the lifted list as `()`, and `()` is an
        // **answer** the backward half gives, not a parameter it takes: `Vjp<F>` is
        // `(values...) => (result, (ct) => (tangents...))`, so only a tangent that is something can be
        // passed in, while the answer has one slot per parameter. Getting this wrong one way makes the
        // half take an argument nobody passes; the other way makes it answer fewer than it declares.
        Type *tangentOfParameter = function->normTypeAt(i + half);
        if (tangentOfParameter != nullptr &&
            camel::core::tangentSpaceOf(tangentOfParameter).hasSpace()) {
            tangentParams.emplace_back(tangentOfParameter, false);
        } else {
            // The parameter's tangent is `()`: an **answer** the half gives, not an argument it
            // takes. It keeps its place in the half's type so that the transposed half answers one
            // slot per parameter -- `Vjp<F>`'s pullback is `(ct) => (..Tangent<ParamsOf<F>>)`, and
            // `Tangent<int>` is `()` -- while the half's *graph* declares no port for it, because
            // nobody can pass a value where there is nothing to pass (the port loop below skips it by
            // the same predicate). Losing the place here makes the pullback answer one member fewer
            // than it declares: `grad_array_with_int.fvm` read `(float[],)` where the declaration
            // said `(float[], ())`.
            tangentParams.emplace_back(TupleType::create(std::vector<Type *>{}), false);
        }
    }

    // The lift's `with` parameters, on the **forward** half only (author, 2026-11-03 15:15: 带 `with`
    // 的函数，pullback 带不带 `with` 列表——不带，闭包捕获). The forward half reads a weight to compute
    // the value, and a weight is a value the call supplies, so its signature declares the list and
    // `addWithPorts` gives it one port per member. The backward half does not: what it reads after the
    // cut is a residual by the one rule -- 后一半要读的东西都是残差，进 `lin` 的闭包 -- and a `with`
    // value is one of them, so `lin` is a function of the tangents alone and `transpose(lin)` is the
    // declared `Pullback<F>` `(ct) => (..Tangent<ParamsOf<F>>)` with no list of its own. The list is
    // still read off the lift's type, so a caller writes the `with` values once, where `vjp`'s lambda
    // takes them.
    param_vec_t withParams;
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        Type *withType = function->withTypeAt(i);
        if (withType == nullptr) {
            halves.why = "a `with` parameter of the function has no type";
            return halves;
        }
        if (function->withIsVarAt(i)) {
            if (Type *value = camel::core::type::valueTypeOf(withType)) {
                withType = value;
            }
        }
        withParams.emplace_back(withType, false);
    }

    // Each half does part of what the source does to the outside world, so each publishes the
    // source's effects: a half whose print nobody depends on does not run at all.
    //
    // The backward half takes the tangents and nothing else. Its residuals are its captures -- a
    // `with` value among them (`residualsOf`) -- so its type holds no residual position and no `with`
    // list: at a call site `lin` is an ordinary function type, so there is no arity a signature could
    // not know, and no guard has to be loosened to admit one.
    halves.backward = FunctionType::create(param_vec_t(), tangentParams, tangent)
                          ->withResources(function->resources());
    // The forward half answers `(result, lin)`: the value and the linear function closed over the
    // residuals it was computed with. `lin`'s type is the backward half's, so the pair's second member
    // is spelled `(tangents...) => tangent` and the closure stays where it belongs -- off the type.
    TupleType *forwardExit = TupleType::create(std::vector<Type *>{result, halves.backward});
    halves.forward         = FunctionType::create(withParams, forwardParams, forwardExit)
                         ->withResources(function->resources());
    halves.closure = closure;
    // What the call `split(f)` answers: the forward half (author, 2026-11-02 05:10 "`split` 的前半交出
    // `(y, lin)`"). There is no second component to hand out: `lin` does not exist until the residuals
    // do, so the forward half is the one that makes it.
    halves.pair = halves.forward;
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
/// One arm of a branch, cut on its own region: the arm's region is made in the half's draft, the
/// half's walk copies the arm's body *into that region* -- the same `copyHalf` the straight-line body
/// uses -- and the value the arm hands the Join comes back. The unit here is a **region**, not an
/// arm-shaped copy of the whole branch (P1-14 of the review): that is what lets the arm of a nested
/// branch enter this function too, one region at a time, instead of an arm's body being copied whole.
bool cutArm(
    Draft &draft, Half &half, const ArmCut &arm, bool tangentSide, gc_node_ref_t brch,
    SplitGraph &refusal, gc_node_ref_t &armResult) {
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
    armResult = half.copyOf[root];
    return true;
}

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
        gc_node_ref_t armResult = rt::kInvalidNodeRef;
        if (!cutArm(draft, half, arm, tangentSide, brch, refusal, armResult)) {
            return false;
        }
        armResults.push_back(armResult);
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

// ---------------------------------------------------------------------------------------------
// A loop's cut (design 4.2; design principles 3.10 and 9.9).
//
// The exit of a lifted loop is the LOOP node itself. A body that is not a loop answers with a tuple
// fill holding (value, tangent); a loop cannot, because its whole value *is* the state and `jvp`
// gives the loop the pair type and the body's state port the pair type -- so the value and the
// tangent leave the node together, as its two components. The cut therefore reads them off the
// node's type and makes two loops over the same rounds:
//
//   * the **forward half** runs the loop the program wrote -- the state alone -- and gives the node
//     one collection slot: one tuple per round, holding the values the backward half reads from
//     that round. The tape is the loop's value component 1, and it crosses the cut inside `lin`'s
//     closure like every other residual (author, 2026-11-02: residuals ride in the closure).
//   * the **backward half** is a loop that *iterates the tape*: one round per round the forward
//     half ran, the round's element being that round's residuals. Its state is the tangent; its
//     body is the tangent half of the lifted body with each read of a forward value replaced by the
//     tape element's member at that position. Iterating the tape is what makes the round pairing
//     right by construction -- round t consumes the element the forward half appended at round t --
//     and it needs no index operator, so `lin` stays an ordinary function of the tangents.
//
// Why this is the reverse-order reading the design asks for (4.2, 9.9, 4.3): `lin` is the
// *linearization*, so its own rounds run forward -- `dx` flows with the data. What must go
// backwards is the *transposed* walk, and the tape is what makes that possible: `transpose` walks
// `lin`'s rounds from the last to the first, reading the tape element that belongs to each round
// (4.3: 倒着走，逐轮取 `split` 留下的残差). The pairing that makes it *correct* rather than merely
// reversed is that each tape element is identified by its round number alone -- element t is the
// state and the reads of round t, and nothing else -- so the reverse walk reads residual t while
// holding the cotangent of turn t. A walk that read them in forward order would pair the cotangent
// of turn t with the state of a different turn; for a scalar state that happens to be invisible in
// the product (scalars commute), which is why the case that pins it is written over a state whose
// per-round maps do not commute, and why the number in that case is the one `grad` prints and not
// just the tangent `lin` prints.

/// The loop a lifted graph's exit is, as the cut needs it: the node, its regions, the pair the body
/// yields, the two halves of the state, and what the backward half reads from a round (the tape) and
/// from outside the loop (the invariants that cross the cut).
struct LoopCut {
    gc_node_ref_t loop = rt::kInvalidNodeRef;
    rt::GCLoopBody body{};
    uint32_t bodyRegion = 0;
    /// The body region's ports: the round's element first (a `for` loop has one), then the state.
    gc_node_ref_t elementPort = rt::kInvalidNodeRef;
    gc_node_ref_t statePort   = rt::kInvalidNodeRef;
    /// The body's yield, read off the pair the region answers with: the next state and its tangent.
    gc_node_ref_t valueRoot   = rt::kInvalidNodeRef;
    gc_node_ref_t tangentRoot = rt::kInvalidNodeRef;
    /// The two projections `jvp` wrote inside the region: the state's value and its tangent. The
    /// forward half hands the first the loop's state port, the backward half hands the second its
    /// own -- a state that is a pair is two ports once it is cut.
    gc_node_ref_t stateValue   = rt::kInvalidNodeRef;
    gc_node_ref_t stateTangent = rt::kInvalidNodeRef;
    /// What the backward half reads from the round: this is the tape, in node order.
    std::vector<gc_node_ref_t> tape;
    /// What the backward half reads from outside the loop: the invariants that cross the cut.
    std::vector<Residual> outside;
    Type *stateValueType   = nullptr;
    Type *stateTangentType = nullptr;
    Type *elementType      = nullptr;
    /// The exit as the program wrote it, when the pair the exit answers with is not the loop cell's own
    /// value but a chain above it: the loop is then *inside* the chain the exit reads (measured
    /// 2026-11-03: `g(s) = (loop x = s ...)[0]` -- the exit is a tuple fill whose two members are
    /// `idx_arr` of the loop's component 0 and component 1). These are the tops of those two chains,
    /// and each half computes its own above the loop it made. `kInvalidNodeRef` on both when the exit
    /// is the cell itself, which is the shape `jvp` gives a loop a function returns whole.
    gc_node_ref_t exitValueRoot   = rt::kInvalidNodeRef;
    gc_node_ref_t exitTangentRoot = rt::kInvalidNodeRef;
    /// The nodes that read one component of the loop's pair through `GCAccsKind::TupleIndex` and stand
    /// outside the body: a half seeds each of them with its own value for that component (the forward
    /// half's `(state, tape)` component 0, the backward half's tangent state), so the chain above the
    /// loop is copied without the walk ever entering the loop cell. Component 0's list is the value
    /// side's, component 1's is the tangent side's.
    std::vector<gc_node_ref_t> valueProjections;
    std::vector<gc_node_ref_t> tangentProjections;
};

/// The nodes of `region` that `root` reads, `root` among them, and the nodes outside the region it
/// reads. A loop's body is a region, not a data input, so the `Half` walk never reaches it -- but
/// the cut has to know what the region reads to say which values ride in the tape and which stay
/// invariants. Staying inside the region is the whole point: a node outside it is computed once,
/// before the loop, and a node inside it is computed once per round.
void regionClosure(
    const rt::GCGraph *graph, uint32_t region, gc_node_ref_t root,
    std::unordered_set<gc_node_ref_t> &inside, std::vector<gc_node_ref_t> &outside) {
    if (graph == nullptr || root >= graph->nodeCount()) {
        return;
    }
    // A region's ports are the round's variables: they are read *inside* the round even though the
    // port node itself is recorded in the body (a port belongs to the graph, not to the region it
    // serves), so they are inside for this walk.
    const std::span<const gc_node_ref_t> ports = graph->regionPorts(region);
    const auto insideRegion = [graph, region, ports](gc_node_ref_t ref) {
        return graph->nodeRegion(ref) == region ||
               std::find(ports.begin(), ports.end(), ref) != ports.end();
    };
    std::vector<gc_node_ref_t> stack{root};
    while (!stack.empty()) {
        const gc_node_ref_t ref = stack.back();
        stack.pop_back();
        if (!inside.insert(ref).second) {
            continue;
        }
        for (gc_node_ref_t input : dataInputsOf(graph, ref)) {
            if (input >= graph->nodeCount() || graph->node(input) == nullptr) {
                continue;
            }
            if (insideRegion(input)) {
                if (!inside.contains(input)) {
                    stack.push_back(input);
                }
            } else if (std::find(outside.begin(), outside.end(), input) == outside.end()) {
                outside.push_back(input);
            }
        }
    }
}

/// Reads the loop the exit is, or says at the node why this loop is not the shape the cut handles.
/// Every refusal here names what is missing rather than saying "not supported": a loop that is
/// lifted but not cut is a reading of its own (`skip=loop ... why=`), and the next cell of the cut
/// is one of these lines, not a second entry.
bool readLoopCut(
    const rt::GCGraph *function, gc_node_ref_t loop, std::ostream &os, LoopCut &cut) {
    const std::string name = function->name();
    const auto refuse = [&](std::string_view what) {
        os << "REINTERP split function=" << name << " skip=loop (" << what << ")\n";
        return false;
    };
    cut.loop = loop;
    cut.body = rt::loopBodyOf(*function, loop);
    // The iterable form does not decide whether a loop can be cut: `jvp` lifts both forms
    // (measured 2026-11-03: an interval loop lifts and answers 16/16). Only the element type differs,
    // and it is read from the tail's own form.
    if (cut.body.collectionCount != 0) {
        return refuse(
            "this loop already collects values, and a cut's tape would have to be merged into a "
            "collection the program asked for");
    }
    for (uint32_t region = 1; region < function->regionCount(); ++region) {
        const rt::GCRegion *one = function->region(region);
        if (one == nullptr || one->owner != loop) {
            continue;
        }
        if (one->kind == rt::GCRegionKind::LoopCond) {
            return refuse(
                "this loop's round count comes from a condition region (`while`), and the cut does "
                "not enter a condition region yet: the tape says how many rounds ran, but the "
                "forward half would have to lift the condition into the same region");
        }
        if (one->kind == rt::GCRegionKind::LoopBody) {
            cut.bodyRegion = region;
        }
    }
    if (cut.bodyRegion == 0) {
        return refuse("this loop has no body region, so there is nothing to cut");
    }
    const std::span<const gc_node_ref_t> ports = function->regionPorts(cut.bodyRegion);
    // One state, and `jvp` writes the round's element as the first port of a `for` loop's body.
    if (ports.size() != 2) {
        return refuse(
            "this loop carries " + std::to_string(ports.size() > 0 ? ports.size() - 1 : 0) +
            " states, and the cut handles one: a tape per state would have to be interleaved, "
            "which the round's single element cannot express");
    }
    cut.elementPort = ports[0];
    cut.statePort   = ports[1];
    const rt::GCNode *stateNode = function->node(cut.statePort);
    if (stateNode == nullptr || stateNode->dataType() == nullptr ||
        stateNode->dataType()->code() != TypeCode::Tuple) {
        return refuse("the loop's state port is not the pair `jvp` writes, so the loop is not lifted");
    }
    const auto *statePair = tt::as_ptr<TupleType>(stateNode->dataType());
    if (statePair->size() != 2) {
        return refuse("the loop's state is not a pair, so it is not a lifted loop");
    }
    cut.stateValueType   = statePair->typeAt(0);
    cut.stateTangentType = statePair->typeAt(1);
    cut.elementType      = function->node(cut.elementPort) != nullptr
                              ? function->node(cut.elementPort)->dataType()
                              : nullptr;
    if (cut.stateValueType == nullptr || cut.stateTangentType == nullptr ||
        cut.elementType == nullptr) {
        return refuse("the loop's state or element has no type");
    }
    // The body's yield: the pair (next state, its tangent). Read off the Fill the region answers
    // with, never guessed from the shape of the region.
    const rt::GCRegion *bodyRegion = function->region(cut.bodyRegion);
    const gc_node_ref_t result =
        bodyRegion != nullptr && bodyRegion->result < function->nodeCount() ? bodyRegion->result
                                                                           : rt::kInvalidNodeRef;
    const std::span<const gc_node_ref_t> yielded = function->withInputsOf(result);
    if (result == rt::kInvalidNodeRef || yielded.size() != 2) {
        return refuse(
            "the loop's body does not yield the pair (next state, tangent), so the loop is not "
            "lifted");
    }
    cut.valueRoot   = yielded[0];
    cut.tangentRoot = yielded[1];
    // The state's two halves, as `jvp` wrote them: projections of the state port inside the region.
    for (gc_node_ref_t ref = 0; ref < function->nodeCount(); ++ref) {
        if (function->nodeRegion(ref) != cut.bodyRegion) {
            continue;
        }
        const rt::GCNode *node = function->node(ref);
        const auto *body       = function->nodeBodyAs<rt::GCAccsBody>(ref);
        const std::span<const gc_node_ref_t> reads = function->normInputsOf(ref);
        if (node == nullptr || node->kind != rt::GCNodeKind::Accs || body == nullptr ||
            body->accsKind != rt::GCAccsKind::TupleIndex || reads.size() != 1 ||
            reads.front() != cut.statePort) {
            continue;
        }
        if (body->value == 0) {
            cut.stateValue = ref;
        } else if (body->value == 1) {
            cut.stateTangent = ref;
        }
    }
    if (cut.stateValue == rt::kInvalidNodeRef || cut.stateTangent == rt::kInvalidNodeRef) {
        return refuse(
            "the body does not read the state's two halves as projections of the state port, so "
            "the cut cannot say which half is the value and which the tangent");
    }
    // What crosses. The tangent side is the closure of the body's tangent; the values it reads that
    // the forward side also has -- computed in the round, or one of the round's ports -- are this
    // loop's tape, and the ones it reads from outside the loop are the invariants.
    std::unordered_set<gc_node_ref_t> valueInside;
    std::vector<gc_node_ref_t> valueOutside;
    regionClosure(function, cut.bodyRegion, cut.valueRoot, valueInside, valueOutside);
    std::unordered_set<gc_node_ref_t> tangentInside;
    std::vector<gc_node_ref_t> tangentOutside;
    regionClosure(function, cut.bodyRegion, cut.tangentRoot, tangentInside, tangentOutside);
    std::vector<gc_node_ref_t> tape;
    for (gc_node_ref_t ref : tangentInside) {
        if (ref == cut.stateTangent) {
            // The state's tangent is not a residual: it is what the backward half's loop carries.
            continue;
        }
        if (ref == cut.elementPort || ref == cut.stateValue) {
            tape.push_back(ref);
            continue;
        }
        // The state port itself is not a value a round reads: the round reads its two halves, and
        // those are the projections. A port of the region is either the element or the state.
        if (ref == cut.statePort) {
            continue;
        }
        if (valueInside.contains(ref)) {
            tape.push_back(ref);
        }
    }
    std::sort(tape.begin(), tape.end());
    cut.tape = std::move(tape);
    // The parameter a graph port is the *value* of, or `kNotAParameter`. A port at a tangent position
    // is the half's own parameter, not a value anything carries, and a port that is no parameter's
    // (a seeded value, a round port) answers `kNotAParameter` too.
    const std::span<const gc_node_ref_t> graphPorts = function->normPorts();
    const auto parameterValueOf = [&](gc_node_ref_t ref) -> size_t {
        const auto at = std::find(graphPorts.begin(), graphPorts.end(), ref);
        if (at == graphPorts.end()) {
            return Residual::kNotAParameter;
        }
        const std::pair<size_t, bool> which =
            parameterOfPort(function->funcType(), static_cast<size_t>(at - graphPorts.begin()));
        return which.second ? Residual::kNotAParameter : which.first;
    };
    std::vector<gc_node_ref_t> invariants;
    for (gc_node_ref_t ref : tangentOutside) {
        const rt::GCNode *node = function->node(ref);
        if (node == nullptr) {
            continue;
        }
        // A constant is written again by whichever half reads it, so it never crosses.
        if (node->kind == rt::GCNodeKind::Data) {
            continue;
        }
        if (node->kind == rt::GCNodeKind::Port) {
            const auto at = std::find(graphPorts.begin(), graphPorts.end(), ref);
            if (at != graphPorts.end() &&
                parameterOfPort(function->funcType(), static_cast<size_t>(at - graphPorts.begin()))
                    .second) {
                // The tangent of a parameter is the backward half's own port, not a residual.
                continue;
            }
        }
        invariants.push_back(ref);
    }
    // **A parameter's own value is a residual when the second half reads it** (author, 2026-11-03
    // 00:50 ①, reworked 03:10). The branch cut carries every value parameter -- its arms may read any
    // of them -- and the loop carries the ones its second half actually reads, which is a fact of the
    // walk rather than of the signature: `int_escape`'s half reads `x` through `3.0*x`, the truncation
    // and `float(...)` even though the round computes none of those, and a signature cannot say so.
    //
    // The walk is the one the copy makes, with the round's own leaves removed: it descends through
    // the values the cut carries as a whole (they are on this side of the cut anyway) and stops at a
    // port whose value the round itself owns -- the state, the round's element -- because the tape
    // carries those and their inputs are not the half's. A constant is skipped for the reason above.
    {
        std::unordered_set<gc_node_ref_t> visited;
        std::vector<gc_node_ref_t> stack{cut.tangentRoot};
        while (!stack.empty()) {
            const gc_node_ref_t ref = stack.back();
            stack.pop_back();
            if (ref >= function->nodeCount() || !visited.insert(ref).second) {
                continue;
            }
            const rt::GCNode *node = function->node(ref);
            if (node == nullptr || node->kind == rt::GCNodeKind::Data) {
                continue;
            }
            if (node->kind == rt::GCNodeKind::Port) {
                const size_t parameter = parameterValueOf(ref);
                if (parameter != Residual::kNotAParameter &&
                    std::find(invariants.begin(), invariants.end(), ref) == invariants.end()) {
                    invariants.push_back(ref);
                }
                continue;
            }
            for (gc_node_ref_t input : dataInputsOf(function, ref)) {
                stack.push_back(input);
            }
        }
    }
    // Program order, the order `residualsOf` numbers for a straight-line cut: parameters by their
    // index first, then the values the forward half computes, by node.
    std::sort(
        invariants.begin(), invariants.end(), [&parameterValueOf](gc_node_ref_t a, gc_node_ref_t b) {
            const size_t pa     = parameterValueOf(a);
            const size_t pb     = parameterValueOf(b);
            const bool aIsValue = pa != Residual::kNotAParameter;
            const bool bIsValue = pb != Residual::kNotAParameter;
            if (aIsValue != bIsValue) {
                return aIsValue;
            }
            if (aIsValue) {
                return pa < pb;
            }
            return a < b;
        });
    for (gc_node_ref_t ref : invariants) {
        Residual one;
        one.node      = ref;
        one.parameter = parameterValueOf(ref);
        one.type      = function->node(ref) != nullptr ? function->node(ref)->dataType() : nullptr;
        if (one.type == nullptr) {
            return refuse("a value the backward half reads from outside the loop has no type");
        }
        cut.outside.push_back(one);
    }
    return true;
}

/// The whole cut of a loop: the forward half runs the primal loop and tapes each round, the backward
/// half loops over that tape and hands the tangent on. Both halves keep the source's round count by
/// construction -- the tape has one element per round the forward half ran.
SplitGraph cutLoopGraph(
    const camel::core::context::context_ptr_t &context, const rt::Program *program,
    rt::GCGraph *function, std::ostream &os, LoopCut &cut) {
    SplitGraph built;
    if (!readLoopCut(function, cut.loop, os, cut)) {
        return built;
    }
    const std::span<const gc_node_ref_t> sourcePorts = function->normPorts();
    // The tape's element type: one tuple per round. A body whose tangent half reads nothing from the
    // round still has to say how many rounds ran (design principles 9.9: 循环体回传不读残差时仍收一个
    // 常量 1，反向循环靠它知道轮数), so the tape is then one `1` per round.
    std::vector<Type *> tapeMemberTypes;
    tapeMemberTypes.reserve(cut.tape.size() + 1);
    for (gc_node_ref_t ref : cut.tape) {
        const rt::GCNode *node = function->node(ref);
        if (node == nullptr || node->dataType() == nullptr) {
            os << "REINTERP split function=" << function->name()
               << " skip=tape (a value the backward half reads from a round has no type)\n";
            return built;
        }
        tapeMemberTypes.push_back(node->dataType());
    }
    const bool countingOnly = tapeMemberTypes.empty();
    if (countingOnly) {
        tapeMemberTypes.push_back(Type::Int64());
    }
    Type *tapeElementType = TupleType::create(tapeMemberTypes);
    Type *tapeType        = ArrayType::create(tapeElementType);
    // What crosses the cut: the invariants the backward half reads from outside the loop, then the
    // tape. The tape has no source node -- it is a value the cut makes -- so it is not in
    // `residualIndexOf`, and the only thing that reads it is the backward half's loop.
    std::vector<Residual> cuts = cut.outside;
    {
        Residual tape;
        tape.node      = rt::kInvalidNodeRef;
        tape.parameter = Residual::kNotAParameter;
        tape.type      = tapeType;
        cuts.push_back(tape);
    }
    std::vector<Type *> cutTypes;
    cutTypes.reserve(cuts.size());
    for (const Residual &one : cuts) {
        cutTypes.push_back(one.type);
    }
    const SplitHalves halves = splitHalvesWith(function->funcType(), TupleType::create(cutTypes));
    if (halves.forward == nullptr || halves.backward == nullptr) {
        os << "REINTERP split function=" << function->name() << " skip=signature (" << halves.why
           << ")\n";
        return built;
    }
    std::unordered_map<gc_node_ref_t, size_t> residualIndexOf;
    for (size_t i = 0; i < cut.outside.size(); ++i) {
        residualIndexOf[cut.outside[i].node] = i;
    }
    // The loop's inputs, as the source writes them: the array it iterates, then its first state.
    const std::span<const gc_node_ref_t> loopInputs = function->normInputsOf(cut.loop);
    if (loopInputs.size() != 2) {
        os << "REINTERP split function=" << function->name()
           << " skip=loop (the loop takes " << loopInputs.size()
           << " inputs, and a `for` loop with one state takes the array and its first state)\n";
        return built;
    }
    const gc_node_ref_t iterable = loopInputs[0];
    const gc_node_ref_t initialState = loopInputs[1];
    const std::span<const gc_node_ref_t> initialHalves = function->withInputsOf(initialState);
    if (initialHalves.size() != 2) {
        os << "REINTERP split function=" << function->name()
           << " skip=loop (the loop's first state is not the pair `jvp` writes, so the loop is not "
              "lifted)\n";
        return built;
    }
    // The chain above the cell answers the half's own result: the forward half's value on the value
    // side, the backward half's tangent on the tangent side. Read off the source graph before either
    // half exists -- a chain that answers something else is not the pair this cut splits, and saying
    // so here keeps the refusal about the shape rather than about a type a copy happened to reach.
    if (cut.exitValueRoot != rt::kInvalidNodeRef || cut.exitTangentRoot != rt::kInvalidNodeRef) {
        const rt::GCNode *valueTop =
            cut.exitValueRoot < function->nodeCount() ? function->node(cut.exitValueRoot) : nullptr;
        const rt::GCNode *tangentTop = cut.exitTangentRoot < function->nodeCount()
                                           ? function->node(cut.exitTangentRoot)
                                           : nullptr;
        const auto *forwardPair =
            halves.forward->exitType() != nullptr &&
                    halves.forward->exitType()->code() == TypeCode::Tuple
                ? tt::as_ptr<TupleType>(halves.forward->exitType())
                : nullptr;
        if (valueTop == nullptr || tangentTop == nullptr || forwardPair == nullptr ||
            forwardPair->size() != 2 || valueTop->dataType() != forwardPair->typeAt(0) ||
            tangentTop->dataType() != halves.backward->exitType()) {
            os << "REINTERP split function=" << function->name()
               << " skip=loop (the chain the exit reads above the loop does not answer the halves' "
                  "results)\n";
            return built;
        }
    }
    const std::string base = "split_of_" + std::string(function->name());
    rt::GraphDraftGroup group(context, program);
    const GraphId forwardId  = group.reserve(base + "_forward", base + "_forward");
    const GraphId backwardId = group.reserve(base + "_backward", base + "_backward");
    {
        // The forward half: the value parameters in, (the loop's value, the tape) out.
        auto &draft = group.transaction(forwardId).edit();
        draft.setFuncType(halves.forward);
        Half forward(draft, function, program, false);
        const size_t valueCount = sourcePorts.size() / 2;
        for (size_t i = 0; i < valueCount; ++i) {
            Type *type               = function->funcType()->normTypeAt(i);
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            forward.portOf[i] = port;
        }
        // The invariants are computed before the loop, in region 0: they are one value each, not one
        // per round.
        for (const Residual &one : cut.outside) {
            if (!forward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=forward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
        }
        // The primal loop: the same rounds, the value state alone, and one collection slot.
        rt::GCLoopBody forwardBody = cut.body;
        forwardBody.collectionCount = 1;
        Type *forwardLoopType =
            TupleType::create(std::vector<Type *>{cut.stateValueType, tapeType});
        const gc_node_ref_t forwardLoop = draft.addLoopNode(forwardBody, forwardLoopType);
        const uint32_t region =
            draft.addRegion(rt::GCRegionKind::LoopBody, 0, forwardLoop, 1);
        draft.setInsertRegion(region);
        std::vector<gc_node_ref_t> regionPorts;
        const gc_node_ref_t element =
            draft.addPortNode(cut.elementType, draft.allocateRuntimeSlot(cut.elementType));
        forward.seed(cut.elementPort, element);
        regionPorts.push_back(element);
        const gc_node_ref_t state =
            draft.addPortNode(cut.stateValueType, draft.allocateRuntimeSlot(cut.stateValueType));
        // The state's value projection *is* this port: the state was a pair, and the forward half's
        // loop carries the value alone.
        forward.seed(cut.stateValue, state);
        regionPorts.push_back(state);
        if (!forward.copyHalf(cut.valueRoot, built)) {
            draft.setInsertRegion(0);
            os << "REINTERP split function=" << function->name()
               << " skip=forward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        std::vector<gc_node_ref_t> tapeMembers;
        tapeMembers.reserve(cut.tape.size() + 1);
        if (countingOnly) {
            tapeMembers.push_back(
                draft.materializeStaticValue(toSlot<int64_t>(1), Type::Int64()));
        } else {
            for (gc_node_ref_t ref : cut.tape) {
                tapeMembers.push_back(forward.copyOf[ref]);
            }
        }
        const gc_node_ref_t tapeTuple = fillTuple(draft, tapeElementType, tapeMembers);
        const std::array<gc_node_ref_t, 2> yielder{forward.copyOf[cut.valueRoot], tapeTuple};
        const gc_node_ref_t bodyResult = fillTuple(
            draft, TupleType::create(std::vector<Type *>{cut.stateValueType, tapeElementType}),
            std::span<const gc_node_ref_t>(yielder.data(), yielder.size()));
        draft.setInsertRegion(0);
        draft.setRegionPorts(region, regionPorts);
        draft.setRegionResult(region, bodyResult);
        // The loop's inputs: the array, then the first state -- which is the value half of the pair
        // `jvp` made for it, not the pair.
        gc_node_ref_t firstState = rt::kInvalidNodeRef;
        {
            const gc_node_ref_t half = initialHalves[0];
            if (!forward.copyHalf(half, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=forward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
            firstState = forward.copyOf[half];
        }
        if (!forward.copyHalf(iterable, built)) {
            os << "REINTERP split function=" << function->name()
               << " skip=forward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        const std::array<gc_node_ref_t, 2> forwardInputs{forward.copyOf[iterable], firstState};
        draft.setNormInputs(
            forwardLoop, std::span<const gc_node_ref_t>(forwardInputs.data(), forwardInputs.size()));
        const gc_node_ref_t forwardRound = draft.addPortNode(
            Type::Int64(), draft.allocateRuntimeSlot(Type::Int64()));
        const std::array<gc_node_ref_t, 2> forwardWith{bodyResult, forwardRound};
        draft.setWithInputs(
            forwardLoop, std::span<const gc_node_ref_t>(forwardWith.data(), forwardWith.size()));
        // The two components of the loop's value: the state the program sees, and the tape.
        const gc_node_ref_t value = draft.addAccsNode(cut.stateValueType, 0);
        draft.setNormInputs(value, std::span<const gc_node_ref_t>(&forwardLoop, 1));
        const gc_node_ref_t tapeValue = draft.addAccsNode(tapeType, 1);
        draft.setNormInputs(tapeValue, std::span<const gc_node_ref_t>(&forwardLoop, 1));
        // The exit the program wrote may read the loop's value through a chain above the cell
        // (`g(s) = (loop ...)[0]`): then the half copies that chain, with the cell's value seeded at
        // its foot, and answers with the top of it rather than with the state whole. The projections
        // are seeded for the *whole* cell's pair, so the walk never reaches the loop node and no body
        // region is copied -- what crosses from the cell into the chain is the one value the half has
        // for that component, and nothing else.
        gc_node_ref_t answered = value;
        if (cut.exitValueRoot != rt::kInvalidNodeRef) {
            for (gc_node_ref_t projection : cut.valueProjections) {
                forward.seed(projection, value);
            }
            if (!forward.copyHalf(cut.exitValueRoot, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=forward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
            answered = forward.copyOf[cut.exitValueRoot];
        }
        std::vector<gc_node_ref_t> residualMembers;
        residualMembers.reserve(cuts.size());
        for (const Residual &one : cut.outside) {
            residualMembers.push_back(forward.copyOf[one.node]);
        }
        residualMembers.push_back(tapeValue);
        const gc_node_ref_t linValue = buildLinearClosure(
            draft, program, backwardId, halves.closure, halves.backward,
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{answered, linValue};
        const gc_node_ref_t out = fillTuple(
            draft, halves.forward->exitType(),
            std::span<const gc_node_ref_t>(exitHalves.data(), exitHalves.size()));
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the tangents in, the tape out of the closure, and a loop over the tape.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(halves.backward);
        Half backward(draft, function, program, true, &cut.outside, &residualIndexOf);
        addResidualCaptures(draft, cuts, backward);
        for (size_t i = sourcePorts.size() / 2; i < sourcePorts.size(); ++i) {
            Type *type               = function->funcType()->normTypeAt(i);
            if (type == nullptr || !camel::core::tangentSpaceOf(type).hasSpace()) {
                // A () slot is an answer this half gives, never an argument it takes.
                continue;
            }
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            backward.portOf[i] = port;
        }
        for (const Residual &one : cut.outside) {
            if (!backward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=backward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
        }
        const gc_node_ref_t tapePort = backward.closurePorts.empty()
                                           ? rt::kInvalidNodeRef
                                           : backward.closurePorts.back();
        if (tapePort == rt::kInvalidNodeRef) {
            os << "REINTERP split function=" << function->name()
               << " skip=backward (the tape has no closure port to be read from)\n";
            return built;
        }
        rt::GCLoopBody backwardBody = cut.body;
        backwardBody.collectionCount = 0;
        // The tape is an array, whatever the primal loop iterated: the backward half walks one
        // element per round the forward half taped, so it is not the interval's pair.
        backwardBody.iterableForm = camel::core::LoopIterableForm::Array;
        const gc_node_ref_t backwardLoop =
            draft.addLoopNode(backwardBody, cut.stateTangentType);
        const uint32_t region =
            draft.addRegion(rt::GCRegionKind::LoopBody, 0, backwardLoop, 1);
        draft.setInsertRegion(region);
        const gc_node_ref_t tapeElement = draft.addPortNode(
            tapeElementType, draft.allocateRuntimeSlot(tapeElementType));
        const gc_node_ref_t state = draft.addPortNode(
            cut.stateTangentType, draft.allocateRuntimeSlot(cut.stateTangentType));
        std::vector<gc_node_ref_t> regionPorts{tapeElement, state};
        // The round's residuals, read out of the tape's element by position, and the tangent state.
        for (size_t i = 0; i < cut.tape.size(); ++i) {
            Type *type = function->node(cut.tape[i])->dataType();
            const gc_node_ref_t part = draft.addAccsNode(type, static_cast<uint32_t>(i));
            draft.setNormInputs(part, std::span<const gc_node_ref_t>(&tapeElement, 1));
            backward.seed(cut.tape[i], part);
        }
        backward.seed(cut.stateTangent, state);
        if (!backward.copyHalf(cut.tangentRoot, built)) {
            draft.setInsertRegion(0);
            os << "REINTERP split function=" << function->name()
               << " skip=backward construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            return built;
        }
        draft.setInsertRegion(0);
        draft.setRegionPorts(region, regionPorts);
        draft.setRegionResult(region, backward.copyOf[cut.tangentRoot]);
        // The loop's inputs: the tape it iterates, then the first tangent -- the tangent half of the
        // pair `jvp` made for the first state.
        gc_node_ref_t firstTangent = rt::kInvalidNodeRef;
        {
            const gc_node_ref_t half = initialHalves[1];
            if (!backward.copyHalf(half, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=backward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
            firstTangent = backward.copyOf[half];
        }
        const std::array<gc_node_ref_t, 2> backwardInputs{tapePort, firstTangent};
        draft.setNormInputs(
            backwardLoop,
            std::span<const gc_node_ref_t>(backwardInputs.data(), backwardInputs.size()));
        const gc_node_ref_t backwardRound = draft.addPortNode(
            Type::Int64(), draft.allocateRuntimeSlot(Type::Int64()));
        const std::array<gc_node_ref_t, 2> backwardWith{
            backward.copyOf[cut.tangentRoot], backwardRound};
        draft.setWithInputs(
            backwardLoop, std::span<const gc_node_ref_t>(backwardWith.data(), backwardWith.size()));
        // The same chain above the cell as the forward half copies, read on the tangent side: the
        // tangent state the loop ends on is seeded into the tangent projection, so what the chain
        // computes is the tangent of what the forward half's chain computed.
        gc_node_ref_t answered = backwardLoop;
        if (cut.exitTangentRoot != rt::kInvalidNodeRef) {
            for (gc_node_ref_t projection : cut.tangentProjections) {
                backward.seed(projection, backwardLoop);
            }
            if (!backward.copyHalf(cut.exitTangentRoot, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=backward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
            answered = backward.copyOf[cut.exitTangentRoot];
        }
        draft.setExitNode(answered);
        draft.setOutputNode(answered);
        draft.setReturnNode(answered, rt::GCReturnKind::Self);
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
       << " residuals=" << built.residuals << " loop=1 tape=" << cut.tape.size()
       << " invariants=" << cut.outside.size();
    if (function->recomputesOnBackward()) {
        os << " recompute=1";
    }
    os << '\n';
    built.program  = group.program();
    built.forward  = const_cast<rt::GCGraph *>(forwardMade);
    built.backward = const_cast<rt::GCGraph *>(backwardMade);
    return built;
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
    // The value half, one residual per parameter in the parameters' order: the lift writes all the
    // values and then one tangent slot each (`jvpSignature`), so the values are the first half of
    // the ports and port k is the k-th parameter's own value.
    const size_t valueCount = sourcePorts.size() / 2;
    for (size_t i = 0; i < valueCount; ++i) {
        Residual one;
        one.node      = sourcePorts[i];
        one.parameter = i;
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
        const size_t valueCount = sourcePorts.size() / 2;
        for (size_t i = 0; i < valueCount; ++i) {
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
                const size_t index = one.parameter;
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
        // `lin` for a branch body: the same closure the straight-line tier builds, closed over the
        // values the cut carries -- the condition among them (design 4.2), which is what lets the
        // backward half pick the arm by the value the forward half picked it by.
        const gc_node_ref_t linValue = buildLinearClosure(
            draft, program, backwardId, branchHalves.closure, branchHalves.backward,
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{join, linValue};
        const gc_node_ref_t out = fillTuple(
            draft, branchHalves.forward->exitType(),
            std::span<const gc_node_ref_t>(exitHalves.data(), exitHalves.size()));
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the seeds in -- the residuals arrive by its closure ports -- and the
        // tangent the picked arm yields out.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(branchHalves.backward);
        Half backward(draft, function, program, true, &cuts, &residualIndexOf);
        addResidualCaptures(draft, cuts, backward);
        for (size_t i = sourcePorts.size() / 2; i < sourcePorts.size(); ++i) {
            Type *type               = function->funcType()->normTypeAt(i);
            if (type == nullptr || !camel::core::tangentSpaceOf(type).hasSpace()) {
                // A () slot is an answer this half gives, never an argument it takes.
                continue;
            }
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
/// Which shape a lifted graph's exit has. Asked once, so that the cut has one entry and four cells
/// instead of a chain of tests: the exit is the pair itself (a straight-line body), a Join a branch
/// names (the condition picks an arm), a Join no branch names (a loop), or something that is not the
/// pair at all. The three that can be cut are cells of the same entry because the cut that will
/// handle them is one traversal over regions -- P1-14 of the review -- and a shape that is refused
/// is a cell too, so that its refusal stays named instead of becoming a fall-through.
enum class ExitShape { Straight, Branch, Loop, NotAPair };

/// One half of a lifted exit, read back to the LOOP cell it comes from: the loop, which of its two
/// components this half reads (0 the value, 1 the tangent), and the nodes that read that component
/// (the projections a half seeds with its own value for the component, so the chain above the loop is
/// copied without the walk entering the cell). `ok` is false when the half reads no loop, or reads
/// two, or reaches one other than through a `TupleIndex` of it.
struct LoopReach {
    bool ok           = false;
    gc_node_ref_t loop = rt::kInvalidNodeRef;
    uint32_t component = 0;
    std::vector<gc_node_ref_t> projections;
};

/// The loop a half of the exit comes from. The walk follows the ordinary data edges and stops at a
/// LOOP node; every node on the way is a node the half copies above the loop, so a value the chain
/// reads from somewhere else (a parameter, another loop) is left to the copy to answer or refuse.
LoopReach loopBehindExit(const rt::GCGraph *function, gc_node_ref_t root) {
    LoopReach reach;
    if (function == nullptr || root >= function->nodeCount() || function->node(root) == nullptr) {
        return reach;
    }
    std::vector<char> visited(static_cast<size_t>(function->nodeCount()), 0);
    std::vector<gc_node_ref_t> stack{root};
    while (!stack.empty()) {
        const gc_node_ref_t ref = stack.back();
        stack.pop_back();
        if (ref >= function->nodeCount() || function->node(ref) == nullptr) {
            continue;
        }
        if (visited[ref] != 0) {
            continue;
        }
        visited[ref] = 1;
        const rt::GCNode *node = function->node(ref);
        if (node->kind == rt::GCNodeKind::Loop) {
            if (reach.loop != rt::kInvalidNodeRef && reach.loop != ref) {
                return LoopReach{}; // two loops: the chain is not a projection of one cell
            }
            reach.loop = ref;
            continue;
        }
        // A projection of the loop's pair is the edge into the cell, and its component is what the
        // half reads. Any other node reading the loop itself would hold the cell's pair as one value,
        // which no half can be handed (`split` hands the components, not the pair).
        const std::vector<gc_node_ref_t> inputs = dataInputsOf(function, ref);
        bool readsTheCell                       = false;
        for (gc_node_ref_t input : inputs) {
            const rt::GCNode *source = input < function->nodeCount() ? function->node(input) : nullptr;
            readsTheCell             = readsTheCell || (source != nullptr &&
                                            source->kind == rt::GCNodeKind::Loop);
        }
        if (readsTheCell) {
            const auto *body = function->nodeBodyAs<rt::GCAccsBody>(ref);
            if (node->kind != rt::GCNodeKind::Accs || body == nullptr ||
                body->accsKind != rt::GCAccsKind::TupleIndex || body->value > 1) {
                return LoopReach{};
            }
            const uint32_t component = static_cast<uint32_t>(body->value);
            if (!reach.projections.empty() && reach.component != component) {
                return LoopReach{}; // one half reads both components: not a projection of one
            }
            reach.component = component;
            reach.projections.push_back(ref);
        }
        for (gc_node_ref_t input : inputs) {
            if (input < function->nodeCount() && visited[input] == 0) {
                stack.push_back(input);
            }
        }
    }
    reach.ok = reach.loop != rt::kInvalidNodeRef && !reach.projections.empty();
    return reach;
}

ExitShape exitShapeOf(const rt::GCGraph *function, gc_node_ref_t exit, LoopCut &cut) {
    const rt::GCNode *node = function->node(exit);
    if (node == nullptr) {
        return ExitShape::NotAPair;
    }
    if (node->kind == rt::GCNodeKind::Fill) {
        const auto *fill = function->nodeBodyAs<rt::GCFillBody>(exit);
        if (fill != nullptr && fill->fillKind == rt::GCFillKind::Tuple &&
            function->withInputsOf(exit).size() == 2) {
            // The pair may be the loop cell itself pair of components, read through a chain above the
            // cell: `jvp` gives a loop the pair type, and a program that reads *into* the loop's value
            // (`g(s) = (loop x = s ...)[0]`) answers the pair by projecting one component on each
            // side. The cut then enters the loop through its cell all the same -- the cell is one node
            // inside the chain the exit reads, not the exit -- and the two chains are what each half
            // computes after the loop. Measured 2026-11-03 (`vjp_loop.rounds_are_undone_in_reverse`):
            // exit 37 = fill[36, 30, 31], 30 = idx_arr(26, 29), 26 = tuple-index 0 of loop 25.
            const std::span<const gc_node_ref_t> halves = function->withInputsOf(exit);
            const LoopReach value   = loopBehindExit(function, halves[0]);
            const LoopReach tangent = loopBehindExit(function, halves[1]);
            if (value.ok && tangent.ok && value.loop == tangent.loop && value.component == 0 &&
                tangent.component == 1) {
                cut.loop             = value.loop;
                cut.exitValueRoot    = halves[0];
                cut.exitTangentRoot  = halves[1];
                cut.valueProjections = value.projections;
                cut.tangentProjections = tangent.projections;
                return ExitShape::Loop;
            }
            return ExitShape::Straight;
        }
    }
    if (node->kind == rt::GCNodeKind::Loop) {
        // A loop answers with its own value: `jvp` gives the LOOP node the pair type, so the exit of
        // `jvp(loop body)` is the loop itself and its two components *are* the pair. There is a pair
        // to cut in that graph all the same -- it is the node's value, not a tuple fill beside it.
        if (function->funcType() != nullptr && node->dataType() == function->funcType()->exitType()) {
            cut.loop = exit;
            return ExitShape::Loop;
        }
        return ExitShape::NotAPair;
    }
    if (node->kind != rt::GCNodeKind::Join || function->funcType() == nullptr ||
        node->dataType() != function->funcType()->exitType()) {
        return ExitShape::NotAPair;
    }
    if (branchConditionOf(function, exit) != rt::kInvalidNodeRef) {
        return ExitShape::Branch;
    }
    cut.loop = exit;
    return ExitShape::Loop;
}

bool answersAPair(const rt::GCGraph *callee, std::string *why) {
    const auto fail = [why](std::string reason) {
        if (why != nullptr) {
            *why = std::move(reason);
        }
        return false;
    };
    if (callee == nullptr) {
        return fail("there is no callee graph to ask");
    }
    if (callee->funcType() == nullptr) {
        return fail("the callee has no function type, so its parameters cannot be counted");
    }
    // The signature side, first: it costs no walk, and a graph that is not a pair by its parameters
    // cannot be one by its exit either (the halves are what the pair answer is written in).
    const SplitHalves halves = splitHalvesWith(callee->funcType(), nullptr);
    if (halves.forward == nullptr || halves.backward == nullptr) {
        return fail(halves.why);
    }
    // The graph side: the exit is the pair itself -- a two-member tuple fill, a loop whose value is
    // the pair, or the Join a branch answers with. `exitShapeOf` is the one reader of that question,
    // so this asks it rather than restating its conditions.
    LoopCut cut;
    const gc_node_ref_t exit = ungated(callee, callee->exitNodeRef());
    if (exitShapeOf(callee, exit, cut) == ExitShape::NotAPair) {
        return fail("its exit is not the pair (value, tangent): it is neither a two-member tuple fill, "
                    "nor a loop whose value is the pair, nor the Join a branch answers with");
    }
    return true;
}

/// The `with` parameters of the source, as ports of one half. A `with` parameter is a value the
/// caller supplies at the call; the lifted body reads it through a `with` port, and a half that did
/// not list such a port would refuse that read with "the port is not a parameter of the graph".
/// So each half that declares the list also declares these ports, in the declaration's order and of
/// the declared type -- the same list `jvpSignature` writes for the lifted graph, minus nothing: a
/// `with` value is not a component of a pair, so it is not halved.
///
/// Only the forward half declares it (author, 2026-11-03 15:15). The backward half reads a `with`
/// value it needs out of `lin`'s closure, where `residualsOf` put it as a residual, so it has no
/// port of its own to give it and no list to state; `lin` being an ordinary function of the tangents
/// is what keeps the pullback's type free of the `with` list.
bool addWithPorts(Half &half, const rt::GCGraph *function) {
    if (function == nullptr || function->funcType() == nullptr) {
        return true;
    }
    const FunctionType *type = function->funcType();
    const size_t withCount   = type->withTypesCount();
    if (withCount == 0) {
        return true;
    }
    const std::span<const gc_node_ref_t> sourceWith = function->withPorts();
    for (size_t i = 0; i < withCount && i < sourceWith.size(); ++i) {
        Type *withType = type->withTypeAt(i);
        if (withType == nullptr) {
            return false;
        }
        if (type->withIsVarAt(i)) {
            if (Type *value = camel::core::type::valueTypeOf(withType)) {
                withType = value;
            }
        }
        const gc_node_ref_t port =
            half.draft.addPortNode(withType, half.draft.allocateRuntimeSlot(withType));
        half.draft.appendWithPort(port);
        half.withPortOf[sourceWith[i]] = port;
    }
    return true;
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
    // **The closure is the residuals, and nothing else.** A parameter's own value the backward half
    // reads is one of them -- `residualsOf` numbers it, and the member carries the parameter number
    // it is the value of (author, 2026-11-03 03:10: 后一半读到的参数原是残差，由 residualsOf 编号进闭包).
    // There is no tail of parameter types after the residuals any more: what a member is the value of
    // is its identity, and a reader that had to count places after the residuals was counting a
    // layout two sides had to agree on.
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
    (void)exitNode;
    // What the exit's shape says about where the loop cell is: filled by the shape question, read by
    // the loop cell of the switch below (the pair's two chains above one loop) and, before that, by
    // the dispatch the loop takes with its own two halves.
    LoopCut loopCut;
    // The loop is the one cell that does not read a residual tuple out of the graph before the cut:
    // its residuals are the tape the forward half makes plus the invariants its rounds read, and
    // neither exists until the loop is read. So it dispatches here, before `residualsOf`, and takes
    // its own two halves with it -- the entry is the same question about the same exit's shape.
    if (exitShapeOf(function, exit, loopCut) == ExitShape::Loop) {
        return cutLoopGraph(context, program, function, os, loopCut);
    }
    // One entry, three cells (P1-14, code review 2026-11-01). What used to be a chain of `if`s --
    // "is it a tuple fill? then straight line; else is it a Join? then branch; else loop; else not a
    // pair" -- is one question about the *shape* of the exit, asked once, and one `switch` over the
    // four answers. The shapes it separates, and why they are cells of one entry rather than three
    // roads:
    //
    // - The pair does not have to be a tuple fill. A branch answers with its Join, and `jvp` gives
    //   that Join the lifted type, so the exit of `jvp(branch body)` is a Join whose slots hold the
    //   pair: one arm writes (value, tangent) into it and the other writes its own. There is a pair
    //   to cut in that graph all the same -- the author's rule for it is design 4.2: the condition
    //   itself is a residual, and the backward half picks the same arm by evaluating that condition.
    //   What `split` cannot do yet is step *inside* an arm: an arm is a region, not a data input, so
    //   `Half`'s walk (which follows `dataInputsOf`) does not reach it. Saying that is the point of
    //   this reading -- a `skip=exit` that does not say which exit leaves the reader with nothing.
    // - A loop is its own shape: the exit of `jvp(loop body)` is the LOOP node, whose two value
    //   components are the pair, and its residuals are the tape the cut makes -- so it is dispatched
    //   above, before `residualsOf` reads a residual tuple the graph does not have (cutLoopGraph).
    // - Every cell *starts* at the same place (the exit's shape) and will end at the same place (a
    //   cut that recurses by region); they are written as cells so that the recursion of P1-14's
    //   next step is a change of one cell's body, not the invention of a second entry.
    switch (exitShapeOf(function, exit, loopCut)) {
        case ExitShape::Straight:
            // The exit is the pair itself: the cut is the straight-line body, and the two `copyHalf`
            // walks after this switch are that cell's one entry.
            break;
        case ExitShape::Branch: {
            const gc_node_ref_t condition = branchConditionOf(function, exit);
            SplitGraph refusal;
            {
                // The first step of the cut a branch needs: enter each arm and say what is in it. The walk a half
                // does follows `dataInputsOf` and never reaches an arm, so this is the entrance it does not
                // have yet -- and the numbers are what the next step builds on (an arm is entered through
                // its form ports, and it is not empty).
                const gc_node_ref_t branch = branchNodeOf(function, exit);
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
                    // The second step: what each arm *yields*. An arm ends in the Fill that writes the
                    // pair into the Join's slot, so its two members are the value and the tangent --
                    // the tangent is what the backward half keeps, and it is read off the Fill's with
                    // inputs, never guessed from the arm's shape.
                    //
                    // No probe draft is made here. This used to clone the whole graph into a draft,
                    // mint a Join in it and drop the lot, once per branch cut, only to learn how many
                    // members an arm has and which values they read from outside it -- numbers the
                    // source graph answers directly (P1-11, code review 2026-11-01). What the reading
                    // prints is unchanged where a plan pins it; the fields that only the clone could
                    // report (`probeRegion`, `probeMembers`, `probeJoin`, `conditionCopied`) are gone,
                    // and `from=[...]` now names source nodes as it always meant to.
                    Type *pairType = exitNode->dataType();
                    // The condition that selects among the arms. A ref that is not a node of this
                    // graph is in no region -- 0 stands for "the body, and so in none of the arms
                    // this loop walks" -- and the region query is not asked about it, because it
                    // would no longer answer the body for a node that is not there.
                    const uint32_t conditionRegion =
                        function->containsNodeRef(condition) ? function->nodeRegion(condition) : 0;
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
                        // The values this arm's members read from *outside* it -- the question a cut
                        // answers before it can carry anything across. Read from the source graph:
                        // a member's region is the arm's, so a value read from another region is a
                        // value that has to cross.
                        std::vector<gc_node_ref_t> fromOutside;
                        for (gc_node_ref_t r = 0; r < function->nodeCount(); ++r) {
                            if (function->nodeRegion(r) != region) {
                                continue;
                            }
                            for (gc_node_ref_t input : dataInputsOf(function, r)) {
                                const rt::GCNode *read = function->node(input);
                                if (read == nullptr || read->kind == rt::GCNodeKind::Data) {
                                    continue; // a constant is not a value the cut has to carry
                                }
                                if (function->nodeRegion(input) == region) {
                                    continue;
                                }
                                if (std::find(fromOutside.begin(), fromOutside.end(), input) ==
                                    fromOutside.end()) {
                                    fromOutside.push_back(input);
                                }
                            }
                        }
                        os << "ARM half=backward arm=" << one->index << " region=" << region
                           << " yield=" << yield << " members=" << members.size() << " value="
                           << (members.size() == 2 ? members[0] : rt::kInvalidNodeRef) << " tangent="
                           << tangent << " inputs=" << fromOutside.size() << " from=[";
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
                           << '\n';
                }
                // The condition picks the arm: the cut hands `cutBranchGraph` the arms it found.
                {
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
        } break;
        case ExitShape::Loop: {
            // **Defensive, and unreachable.** Two shapes answer `ExitShape::Loop`, and neither reaches
            // this cell any more: a LOOP node whose type is the lifted pair is cut above, before
            // `residualsOf` (its residuals are the tape the cut makes, so they do not exist yet when
            // the switch is entered); and a Join that no branch names is a graph `jvp` does not build,
            // so no program in the language makes one (measured: no plan pins `skip=loop`, because no
            // case can print it). The refusal stays, and stays phrased as a refusal, because a graph
            // *could* be built with either shape by another transform, and a cut that met one silently
            // would be worse than one that names it. This is "cannot happen here", not "not supported
            // yet": the second is a promise to do it and the first is a statement about the graphs
            // this tree makes.
            SplitGraph refusal;
            refuseAt(
                refusal, function, exit, "the exit",
                "this graph's exit is a Join that holds the pair and no branch names it, so the two "
                "halves' values meet at a loop -- a loop's residual is what one turn hands the next "
                "(design 4.2), and a half's walk does not enter a region yet");
            os << "REINTERP split function=" << function->name() << " skip=loop exit=Join"
               << " why=" << refusal.refusedWhy << '\n';
            return refusal;
        }
        case ExitShape::NotAPair: {
            // **Reachable from `.cml`**, and the cell used to say the opposite ("defensive, and
            // unreachable"), which was wrong twice over. `split`'s own type refuses a plain function
            // before this function runs -- `Invalid call to 'split' with argument types <> ((x: float)
            // => float pure)` -- so a graph built by hand is one way in; a lifted graph is the other,
            // and it is the way a program takes. The shape: the exit node is the *application* whose
            // lifted copy is the pair (measured with a temporary probe line, 2026-11-03: `kind=10`
            // -- `GCNodeKind::Func` -- with two `with` inputs and two ordinary ones, and the copy's
            // type `(float, float)`), so the pair is a callee's answer and not the two-member tuple
            // `Fill` this graph writes itself. That is what a differentiated lambda ending in a call
            // to a `with`-parameterised function lifts to
            // (`test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml`). The
            // `exit=Tuple{chain,tangent}` field of the `REINTERP jvp` line is a fixed label, not a
            // reading, and does not say this: the node's kind does.
            //
            // So this is a wall to name, not a state to defend: the three routes through it -- write
            // the pair into a `Fill` in the lowering, teach this switch the call's-pair shape, or work
            // around it in the library -- are weighed in
            // `docs/handover/questions/2026-11-03-when-a-lambda-ends-with-a-with-call-what-does-split-cut.cn.md`,
            // which takes the first: an exit's shape is the lowering's product, while this switch is
            // the semantic invariant, and a criterion naming more shapes is harder to state than a
            // product that writes the one shape.
            SplitGraph refusal;
            refuseAt(
                refusal, function, exit, "the exit",
                "a lifted function answers with the pair (result, tangent); here the pair is a call's "
                "result -- the differentiated lambda ends in a call to a `with`-parameterised function "
                "-- and not the two-member tuple fill this cut takes its two halves from, so there is "
                "no pair to cut. This shape is not accepted: "
                "`docs/handover/questions/2026-11-03-when-a-lambda-ends-with-a-with-call-what-does-"
                "split-cut.cn.md` weighs the three routes through it");
            os << "REINTERP split function=" << function->name() << " skip=exit ("
               << refusal.refusedWhy << ")\n";
            return refusal;
        }
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

    // **The cuts this one's value half needs, and the table it reads them from.** A call to a callee
    // that answers the pair is answered by that callee's own forward half, so the callee's cut has to
    // exist -- and the id has to be known before either half here is built, which is what a body that
    // calls itself needs: the entry for this graph is in the table already, and a self-call names the
    // graph this very build is writing. The table lives only for the length of one transform call: it
    // is not part of any graph and nothing in the core knows about it (author, 2026-11-03 17:55, the
    // same shape `jvp` uses for its own recursion).
    std::unordered_map<const rt::GCGraph *, CutMember> calleeCuts;
    // **The programs whose graphs this build stands on.** A nested build's two halves are graphs its
    // own program owns, so adopting that program brings them; the graphs a nested build *adopted* are
    // owned by the program they were made in, which is this list (`SplitGraph::nestedPrograms`). One
    // list for both levels, in the order they were taken and without repeats: `Program::adoptMissing`
    // binds what the program it reads owns and skips what is already bound, so the order says which
    // program answers for an id and the repeat would only bump a version.
    std::vector<const rt::Program *> nestedPrograms;
    const auto remember = [&nestedPrograms](const rt::Program *standsOn) {
        if (standsOn != nullptr &&
            std::find(nestedPrograms.begin(), nestedPrograms.end(), standsOn) ==
                nestedPrograms.end()) {
            nestedPrograms.push_back(standsOn);
        }
    };
    {
        CutMember self;
        self.forwardId = forwardId;
        self.answer    = halves.forward->exitType();
        calleeCuts.emplace(function, self);
    }
    std::vector<const rt::GCGraph *> pending{function};
    for (size_t at = 0; at < pending.size(); ++at) {
        for (const rt::GCGraph *callee : pairCalleesOnValueChain(*pending[at], program)) {
            if (callee == nullptr || calleeCuts.find(callee) != calleeCuts.end()) {
                continue;
            }
            if (callee->funcType() == nullptr) {
                continue;
            }
            // The callee's own cut, built here and now, in a group of its own: the two members it
            // makes are graphs of this context, and this build's program adopts them below, so that
            // the call this forward half writes resolves at run time.
            const SplitGraph nested = buildSplitGraph(context, program, const_cast<rt::GCGraph *>(callee), os);
            if (nested.forward == nullptr || nested.program == nullptr) {
                os << "REINTERP split function=" << function->name()
                   << " skip=callee (the cut of `" << callee->name() << "`, which this body calls, "
                   << "does not build: "
                   << (nested.refusedWhy.empty() ? "no reason was given" : nested.refusedWhy) << ")\n";
                return SplitGraph{};
            }
            CutMember member;
            member.forwardId = nested.forward->id();
            member.answer    = nested.forward->funcType() != nullptr
                                   ? nested.forward->funcType()->exitType()
                                   : nullptr;
            if (member.answer == nullptr) {
                os << "REINTERP split function=" << function->name() << " skip=callee (the forward "
                   << "half of `" << callee->name() << "` declares no exit type)\n";
                return SplitGraph{};
            }
            // **A cut of a cut.** The forward half of `callee` that this build calls is not the whole
            // answer when that half is itself answered by a call of *its* callee's forward half: the
            // nested build made that second cut and adopted its two graphs into its own program, and
            // this build adopts that program in turn -- but `Program::adoptMissing` copies only the
            // graphs the program it reads **owns**, and an adopted graph is not one it owns. So
            // adopting the nested program alone would leave this one holding the halves of `callee`
            // and not the halves of `callee`'s callee, and the call the value half writes to the
            // deeper forward half would name a graph the program does not hold -- `runtimeTarget !=
            // nullptr`, "a direct call names a function whose graph is not in the program's graph
            // table" in `nodevm.cpp` (measured 2026-11-04 on `split(jvp(f))` where `f` calls `g` and
            // `g` calls `h`: the cut of `jvp_of_f` answers a call of `split_of_jvp_of_g_forward`,
            // which answers one of `split_of_jvp_of_h_forward`).
            //
            // So the list is **transitive**: the programs this build stands on are the ones the
            // nested builds stand on, one level down and however much further, because the field is
            // collected the same way one level out. Nothing else travels: every program in it is one
            // a cut this build calls was built in, so the graphs are the calls' own halves and not
            // some other function's. A depth of N cuts asks for N levels of programs, and each level adds the
            // pair of graphs its own callee's cut made and no more: two levels read
            // `test/cases/transforms/split/split_two_nested_cuts.cml`, three and four were probed at
            // depth (the readings are in
            // `docs/handover/questions/2026-11-04-who-adopts-the-graphs-of-a-nested-cut.cn.md`).
            remember(nested.program);
            for (const rt::Program *standsOn : nested.nestedPrograms) {
                remember(standsOn);
            }
            calleeCuts.emplace(callee, member);
            pending.push_back(callee);
        }
    }

    SplitGraph built;
    {
        // The forward half: the value of every parameter in, (result, residuals) out.
        auto &draft = group.transaction(forwardId).edit();
        draft.setFuncType(halves.forward);
        Half forward(draft, function, program, false, nullptr, nullptr, &calleeCuts);
        if (!addWithPorts(forward, function)) {
            os << "REINTERP split function=" << function->name()
               << " skip=forward (a `with` parameter of the source has no type)\n";
            return SplitGraph{};
        }
        // The lifted list is two halves -- the values, then their tangents -- so the forward half
        // takes the first half of it, one port per parameter value.
        const size_t valueCount = sourcePorts.size() / 2;
        for (size_t i = 0; i < valueCount; ++i) {
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
        // A call whose **linear half** crosses the cut is made by this half even when nothing on the
        // result side reads the value it answered: `lin` only exists where the call was made, and it
        // is the thing the second half calls. So the walk is extended to it, values and all.
        for (const Residual &residual : graphResiduals) {
            if (!residual.linearHalf || residual.node >= function->nodeCount() ||
                forward.copyOf[residual.node] != rt::kInvalidNodeRef) {
                continue;
            }
            if (!forward.copyHalf(residual.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=forward construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                return built;
            }
        }
        // The residuals: the values `residualsOf` named, in its order -- the forward half computes them all
        // (they are computed before the cut, that is what makes them residuals) and hands them on inside
        // the `lin` it builds. A parameter's value is one of them when the backward half reads it; the
        // identity graph reads nothing, so `lin` captures nothing.
        std::vector<gc_node_ref_t> residualMembers;
        residualMembers.reserve(graphResiduals.size());
        for (const Residual &residual : graphResiduals) {
            if (residual.linearHalf) {
                // The second component of what this half's call answered: the callee's own linear half,
                // closed over whatever its body reads. The call site never sees those values, and this
                // projection is the whole of what it takes to hand them on.
                const gc_node_ref_t call =
                    residual.node < function->nodeCount() ? forward.copyOf[residual.node]
                                                          : rt::kInvalidNodeRef;
                if (call == rt::kInvalidNodeRef) {
                    os << "REINTERP split function=" << function->name()
                       << " skip=residual (the forward half does not make the call whose linear half "
                          "the cut carries)\n";
                    return built;
                }
                const gc_node_ref_t lin =
                    draft.addAccsNode(residual.type, 1, 0);
                draft.setNormInputs(lin, std::span<const gc_node_ref_t>(&call, 1));
                residualMembers.push_back(lin);
                continue;
            }
            gc_node_ref_t value = residual.node < function->nodeCount()
                                      ? forward.copyOf[residual.node]
                                      : rt::kInvalidNodeRef;
            if (value == rt::kInvalidNodeRef && residual.parameter != Residual::kNotAParameter) {
                // A value parameter the forward half's walk did not copy still crosses: the half's
                // port for it is the value itself, which is what a residual is for this kind of
                // member. The branch member below makes the same fallback, and the two have to
                // agree -- `residualsOf` names the same residuals for both, and a parameter the
                // backward half reads is one of them.
                const size_t portIndex = residual.parameter;
                if (portIndex < forward.portOf.size()) {
                    value = forward.portOf[portIndex];
                }
            }
            if (value == rt::kInvalidNodeRef) {
                // A `with` port the backward half reads: the forward half holds the port that
                // receives that value at the call (`addWithPorts`), and that port is what crosses in
                // the closure -- nothing is computed for it, so no walk reaches it unless the result
                // side happens to read the same weight.
                const auto withPort = forward.withPortOf.find(residual.node);
                if (withPort != forward.withPortOf.end()) {
                    value = withPort->second;
                }
            }
            if (value == rt::kInvalidNodeRef) {
                os << "REINTERP split function=" << function->name()
                   << " skip=residual (the forward half does not compute a value the cut carries)\n";
                return built;
            }
            residualMembers.push_back(value);
        }
        // `lin`: the backward graph closed over those values, built here because here is where they
        // exist. Its closure layout is the one this build read from the backward graph, so the
        // collector scans it by exactly the type the graph states.
        const gc_node_ref_t linValue = buildLinearClosure(
            draft, program, backwardId, halves.closure, halves.backward,
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{forward.copyOf[resultHalf], linValue};
        const gc_node_ref_t out = fillTuple(
            draft, halves.forward->exitType(),
            std::span<const gc_node_ref_t>(exitHalves.data(), exitHalves.size()));
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the tangent of every parameter in -- the residuals arrive by its closure
        // ports, a `with` value among them -- and the tangent out. No `with` list is declared here
        // (author, 2026-11-03 15:15), so there are no `with` ports to make either: a read of one is
        // a read of the closure port `addResidualCaptures` gave it.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(halves.backward);
        std::unordered_map<gc_node_ref_t, size_t> residualIndexOf;
        for (size_t i = 0; i < graphResiduals.size(); ++i) {
            residualIndexOf[graphResiduals[i].node] = i;
        }
        Half backward(draft, function, program, true, &graphResiduals, &residualIndexOf);
        addResidualCaptures(draft, graphResiduals, backward);
        for (size_t i = sourcePorts.size() / 2; i < sourcePorts.size(); ++i) {
            Type *type       = function->funcType()->normTypeAt(i);
            if (type == nullptr || !camel::core::tangentSpaceOf(type).hasSpace()) {
                // A () slot is an answer this half gives, never an argument it takes.
                continue;
            }
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
    // The programs of the cuts this one stands on: the graphs of those halves join this build's
    // program, so the calls the halves wrote resolve at run time -- the two members just encoded, and
    // the deeper pairs the nested builds only adopted. Adopted, not rebuilt: the ids the calls name
    // are the graphs those builds made, and no value is recomputed.
    for (const rt::Program *nested : nestedPrograms) {
        group.adopt(*nested);
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
    // What this cut stands on, past the graphs `program` already holds: a caller one level out
    // cannot adopt them (`Program::adoptMissing` copies owned graphs only), so it reads this list.
    // Transitive by construction -- the nested builds' own lists are in it -- so a caller that adopts
    // every program named here holds every half a call of this cut can reach.
    built.nestedPrograms = nestedPrograms;
    return built;
}

} // namespace camel::transforms
