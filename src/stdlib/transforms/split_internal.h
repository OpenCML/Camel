// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What `split.cpp` and `split_loop_cut.cpp` share: the reading helpers both ask (what a node takes,
 * which port is a value, whether a callee answers a pair, what a tuple fill is), the two structs
 * the build passes around (`Half`, the member being written, and `CutMember`, a cut already made),
 * and the loop cut -- the node and the regions it is read as, and the two functions that read and
 * build it. A helper is declared here when the other file calls it; the ones only `split.cpp` uses
 * stay there.
 */

#pragma once

#include "camel/runtime/graph.h"
#include "graph/graph_view.h"
#include "split.h"

#include "jvp.h"
#include "lift.h"
#include "node_operator.h"
#include "refusal_readings.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
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

using camel::runtime::gateWaitsOf;
using camel::runtime::valueBehindGates;

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

// ---------------------------------------------------------------------------------------------
// The readings both translation units ask of a graph. A declaration here is a call across the two
// files; the definition is the one `split.cpp` already had, and `split.cpp` keeps it.
// ---------------------------------------------------------------------------------------------

/// A refusal, said at the node of the source graph that caused it: the construct the plumbing does
/// not copy, and why. A build that answered with a graph anyway would be a graph that computes
/// something else (design principles 9.9).
bool refuseAt(
    SplitGraph &refusal, const rt::GCGraph *source, gc_node_ref_t ref, std::string construct,
    std::string why);

/// The node's data inputs: its norm inputs followed by its with inputs, in order.
std::vector<gc_node_ref_t> dataInputsOf(const rt::GCGraph *graph, gc_node_ref_t ref);

/// A `Tuple` fill of `members`, with the empty tuple as the static operand the fill reads.
gc_node_ref_t fillTuple(rt::GraphDraft &draft, Type *type, std::span<const gc_node_ref_t> members);

/// The type of the operator itself: `f`'s lifted type with the pair it answers as the exit.
FunctionType *splitOperatorType(FunctionType *function);

/// The lifted signature a call node passes a value for, or null when it passes no function value or
/// the value's type is not one.
camel::core::type::FunctionType *
callValueSignatureOf(const camel::graph::GraphView &view, gc_node_ref_t ref);

/// The ports of a lifted signature that the caller passes values for: the ones whose paired
/// position has a tangent space, which is the column `parameterOfPort` numbers.
std::vector<size_t> valuePortsOf(const camel::core::type::FunctionType *liftedType);

/// Which parameter a lifted signature's port is the value of, and whether it is that parameter's
/// tangent: port `k < half` is parameter `k`'s value, port `half + k` is its tangent.
std::pair<size_t, bool>
parameterOfPort(const camel::core::type::FunctionType *liftedType, size_t port);

/// The graph a call node names, or null when it names none (or the program does not have it).
const rt::GCGraph *
calleeGraphOf(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref);

/// Whether the value a call at `ref` passes is a function that answers a pair.
bool answersAPairByValueAt(const rt::GCGraph &graph, gc_node_ref_t ref);

/// Whether `ref` is a call to a function that answers a pair -- by the value it passes or by the
/// callee it names.
bool answersAPairAt(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref);

/// The tangents of a call to a pair-answering callee: the second half of the arguments the call
/// passes, minus the `()` slots.
std::vector<gc_node_ref_t>
tangentArgumentsOf(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref);

/// The call whose pair's **tangent** component `ref` reads, when `ref` is that read.
gc_node_ref_t
pairTangentReadOf(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref);

/// The cut a callee already has in this build: the graph its forward half is, and the type that
/// half answers. Both are reserved before either member is built, so a body that calls itself -- or
/// two functions that call each other -- names the graph it is being built into. The table lives
/// only for the length of one transform call: it is not part of any graph and it is not in the
/// core.
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
    // program-holder-exempt: borrow-or-foreign-type -- the half reads the program its pass was
    // given; the pass keeps it for the build.
    const rt::Program *program;
    std::vector<gc_node_ref_t> portOf;
    std::unordered_map<gc_node_ref_t, size_t> portIndex;
    /// A `with` parameter of the source graph -> this half's port for it. A `with` value is not a
    /// component of a pair and not a residual: it is a value the caller supplies to the call, and
    /// the half reads it the way the lifted body did, through a port of its own (see
    /// `addWithPorts`). Kept beside `portIndex` rather than in it, because that map is the ordinary
    /// parameters' -- the pair positions `valuePortsOf` and `parameterOfPort` number.
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> withPortOf;
    /// A value the source graph **captured** from where it was written -> this half's port for it
    /// (`addCapturePorts`). A capture is not a `with` parameter and arrives by neither of the two
    /// roads the other maps name: it is read through a closure node of the source, and the half
    /// receives it out of the function value's closure tuple, which the operator copies over
    /// (`split_operator.cpp`, `splitKernel`: `f`'s tuple goes to the forward half unchanged).
    /// A closure port is forwarded into the member's own port, and the call site fills it as usual.
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> capturePortOf;
    std::vector<gc_node_ref_t> copyOf;
    std::vector<char> seen;
    /// Which component of every parameter this half takes: false the value, true the tangent.
    bool tangent = false;
    /// The backward half's residual captures: one closure port per residual, in the order
    /// `residualsOf` numbered them. A residual travels in the closure tuple of the `lin` the
    /// forward half builds, not in a parameter of this half -- which is
    /// what keeps this half an ordinary function type and the residual count out of every
    /// signature.
    std::vector<gc_node_ref_t> closurePorts;
    /// What crosses the cut, when this half is the backward one: a source node the forward half
    /// computes and this half reads is *not* rebuilt here -- it enters by the closure port of its
    /// place in the list. Nothing is recomputed on this side of the cut.
    const std::vector<Residual> *residuals                           = nullptr;
    const std::unordered_map<gc_node_ref_t, size_t> *residualIndexOf = nullptr;
    /// The cuts this build already made, keyed by the graph they cut; null when this half is built
    /// outside one -- a branch or a loop body builds its members one at a time, and a call to a
    /// pair-answering callee inside one is refused by name rather than answered with a graph that
    /// does not exist. The value half answers such a call with the callee's **forward half**, so it
    /// needs that graph's id and its answer type before either is built.
    const std::unordered_map<const rt::GCGraph *, CutMember> *calleeCuts = nullptr;

    Half(
        rt::GraphDraft &memberDraft, const rt::GCGraph *sourceGraph,
        const rt::Program *programGraph, bool tangentHalf,
        const std::vector<Residual> *crossing                            = nullptr,
        const std::unordered_map<gc_node_ref_t, size_t> *crossingIndexOf = nullptr,
        const std::unordered_map<const rt::GCGraph *, CutMember> *cuts   = nullptr)
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

    /// The source inputs this half takes from `ref`: a call to a function that answers a pair takes
    /// the values its caller passes, which are the lifted signature's value ports (read from its
    /// types); on the backward half, the read of such a call's tangent component takes the call's
    /// tangent arguments, because the value it stands for is `lin_g` applied to them. Every other
    /// node takes all of them, in order.
    std::vector<gc_node_ref_t> halfInputsOf(gc_node_ref_t ref) const {
        const std::vector<gc_node_ref_t> all = dataInputsOf(source->view(), ref);
        if (tangent) {
            const gc_node_ref_t call = pairTangentReadOf(*source, program, ref);
            return call != rt::kInvalidNodeRef ? tangentArgumentsOf(*source, program, call) : all;
        }
        if (!answersAPairAt(*source, program, ref)) {
            return all;
        }
        // Which half of the call's list this half takes is the **callee's signature's** answer, and
        // it is read from the type: `valuePortsOf` asks each port of `Lift<F>` whether the port
        // `half` places on is its tangent space, and the ports that pass are the values the caller
        // passes. For a call by name that is the callee graph's type; for a call through a value it
        // is the value's own type (there is no graph yet).
        //
        // Neither a record of what the call passes nor a count is a reading here: the callee's
        // signature already says which ports are values, and a second record of the same positions
        // (`GCNode::tangentArgs`) would make the answer depend on which side wrote it -- a call a
        // rule's expansion wrote and a call `jvp` built carry the record, a call this transform
        // builds forwards whatever its source had, and none of that is what the seam is. And the
        // position of the tangents in a call's argument list is not the signature's arithmetic
        // either, because a function value passed as an argument is an ordinary data input and a
        // call through a value has no callee among its data inputs at all.
        const camel::core::type::FunctionType *liftedType =
            callValueSignatureOf(source->view(), ref);
        const auto *callee = liftedType != nullptr ? nullptr : calleeOf(ref);
        if (liftedType == nullptr) {
            liftedType = callee != nullptr ? callee->funcType() : source->funcType();
        }
        const std::vector<size_t> valuePorts = valuePortsOf(liftedType);
        if (valuePorts.empty()) {
            // The callee's signature is not a lift: there is no half to read, and a half that took
            // some of the arguments and not others would place the rest in the wrong slots.
            return all;
        }
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
        // is a call the runtime refuses by name (for example `FUNC node 2 passing 0
        // arguments and 1 'with' arguments to 'weighted', which takes 1 and 1`).
        for (gc_node_ref_t input : source->withInputsOf(ref)) {
            values.push_back(input);
        }
        return values;
    }

    /// The port residual `index` enters by: the closure port this half declared for it. The values
    /// are in the closure tuple of the `lin` the forward half builds, in `residualsOf`' order, so a
    /// read of one is the port itself -- no projection node, and no count in any type.
    gc_node_ref_t projectResidual(size_t index, SplitGraph &refusal) {
        if (index >= closurePorts.size()) {
            refuseAt(
                refusal,
                source,
                rt::kInvalidNodeRef,
                "a residual",
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

    /// Pre-seeds the copy of a source node: this half already knows what `ref` is on its side of
    /// the cut -- a loop region's port, or a value the tape hands the round -- so the walk must
    /// neither copy it again nor follow what computes it. `seen` is set with `copyOf`, because the
    /// walk asks `seen` before it asks `copyOf`.
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
    /// `liftConstant` does the same for its constants). This is the one writer: the reachability
    /// walk and the DATA case both write a constant through it, so the two cannot disagree about
    /// how a constant becomes a node of a half.
    gc_node_ref_t writeConstant(gc_node_ref_t ref) {
        if (ref >= source->nodeCount()) {
            return rt::kInvalidNodeRef;
        }
        const rt::GCNode *node = source->node(ref);
        if (node == nullptr || node->kind != rt::GCNodeKind::Data) {
            return rt::kInvalidNodeRef;
        }
        const std::span<const slot_t> slots = source->staticSlots();
        const std::ptrdiff_t index          = -static_cast<std::ptrdiff_t>(node->dataIndex);
        if (node->dataIndex >= 0 || index < 0 || static_cast<size_t>(index) >= slots.size()) {
            return rt::kInvalidNodeRef;
        }
        return draft.materializeStaticValue(slots[static_cast<size_t>(index)], node->dataType());
    }

    /// Places a call's arguments on its copy: the ordinary ones as the call's norm inputs and the
    /// context ones as its `with` inputs. `halfInputsOf` is `dataInputsOf`, which is the ordinary
    /// inputs followed by the context ones (`dataInputsOf` above), so a copy that hands that one
    /// list to `setNormInputs` passes the call's `with` argument as an ordinary one -- and a call
    /// the graph names is checked against what the callee declares at run time. For example, on the
    /// composition `vjp((ww) => weighted<ww>(2.0))(3.0)`: `Runtime graph
    /// 'split_of_jvp_of___g_247___forward' has FUNC node 2 passing 1 arguments and 0 'with'
    /// arguments to 'weighted', which takes 1 and 1`.
    void placeCallArguments(
        gc_node_ref_t made, gc_node_ref_t ref, const std::vector<gc_node_ref_t> &placed) {
        size_t withCount = source->withInputsOf(ref).size();
        if (withCount > placed.size()) {
            withCount = placed.size();
        }
        const size_t normCount = placed.size() - withCount;
        draft.setNormInputs(made, std::span<const gc_node_ref_t>(placed.data(), normCount));
        draft.setWithInputs(
            made,
            std::span<const gc_node_ref_t>(placed.data() + normCount, withCount));
    }

    /// The copy of one source node, its inputs already copied. The kinds below are the tier the
    /// plumbing copies; every other kind is refused at the node.
    bool makeCopy(gc_node_ref_t ref, SplitGraph &refusal) {
        if (crossesCut(ref)) {
            const size_t at = residualIndexOf->at(ref);
            if (residuals != nullptr && at < residuals->size() && (*residuals)[at].linearHalf) {
                // The call itself is the residual, not a value of it: what crosses the cut is
                // `lin_g`, which the forward half read as the second component of what its call
                // answered. A reader that wants the pair whole wants something neither half has.
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "the pair a call answers",
                    "the backward half reads the pair itself, and what crosses the cut is only the "
                    "linear half of it -- the second half of a call reads `lin` and calls it on "
                    "the "
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
        // The effect runs (control, order, sync) are not data: a node that has one stands in a
        // block or on a chain, and copying its values without its place would move a statement.
        // Refused by name, never dropped.
        if (!source->ctrlInputsOf(ref).empty() || !source->orderPredsOf(ref).empty() ||
            !source->syncPredsOf(ref).empty()) {
            return refuseAt(
                refusal,
                source,
                ref,
                "a node on an effect edge",
                "the plumbing copies the data ports of a straight-line body; the control, order "
                "and "
                "sync edges are a later tier");
        }
        // A branch and a loop are refused here, before their inputs are asked for. A loop's inputs
        // are the body's yield -- a node of another region -- and the round counter it writes into
        // a port that is nobody's parameter, so asking for them first answers "an operand of the
        // node is not reachable from the half's exit" (or, deeper, "a port") about a graph whose
        // answer is "a loop". The answer is the same either way; only the sentence is not
        // (measured: jvp of a loop of two states).
        switch (node->kind) {
        case rt::GCNodeKind::Brch:
        case rt::GCNodeKind::Join:
            return refuseAt(
                refusal,
                source,
                ref,
                "a branch",
                "the plumbing copies straight-line bodies");
        case rt::GCNodeKind::Loop:
            // A loop the cut can copy is one the exit reads through its two projections:
            // `exitShapeOf` answers that shape, records the pair of `TupleIndex` reads and seeds
            // each half with its own value for the component it reads, so the walk stops at the
            // projection and the cell is never entered. A loop reached here is on the chain the
            // exit reads without that pairing -- read whole, or from one side only -- and a loop
            // cell is not a value: its inputs are the body's yield, a node of another region, and
            // the round counter it writes into a port that is nobody's parameter. Copying it means
            // copying a body region.
            return refuseAt(
                refusal,
                source,
                ref,
                "a loop",
                "this loop is on the chain the exit reads and neither half has a value for it: the "
                "cut "
                "copies a loop where the exit reads its two components through one projection "
                "each, and "
                "here the half would have to copy a body region");
        default:
            break;
        }
        // Every input must be available: either copied already, or one of the values that cross the
        // cut, which this half takes out of the residual tuple at its place in the list. A graph
        // the walk could not finish (a cycle, a reference that is not a node) is a refusal, not a
        // node with a hole in its inputs.
        std::vector<gc_node_ref_t> inputs;
        for (gc_node_ref_t input : halfInputsOf(ref)) {
            // A constant has no producer, so either half can write it again: a half that reads one
            // takes it as available rather than as an operand that failed to arrive. Without this
            // the answer would turn on where a literal sits and not on what the half computes -- a
            // constant on a value operand is a value like any other, so a body whose only
            // peculiarity is a bare constant there cuts like any other body. A constant whose slot
            // this build cannot read is not written here, and the operand then fails to arrive.
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
                    refusal,
                    source,
                    ref,
                    "a node whose input was not copied",
                    "an operand of the node is not reachable from the half's exit");
            }
            inputs.push_back(copyOf[input]);
        }
        // The four families read as ordinary operators are cases by operator; every other
        // kind is a case by its kind (`node_operator.h`).
        const NodeCase nodeCase = nodeCaseOf(*source, ref);
        switch (nodeCase) {
        case NodeCase::Port: {
            // A `with` parameter is a value the caller supplies, not a component of a pair: this
            // half made a port of its own for it, and the read is that port.
            const auto with = withPortOf.find(ref);
            if (with != withPortOf.end()) {
                copyOf[ref] = with->second;
                return true;
            }
            const auto at = portIndex.find(ref);
            if (at == portIndex.end()) {
                // A value the body captured from where it was written: the half declared a port
                // for it out of the source's closure (`addCapturePorts`), and the read is that
                // port. The value comes in with the function value, not as an argument.
                const auto captured = capturePortOf.find(ref);
                if (captured != capturePortOf.end()) {
                    copyOf[ref] = captured->second;
                    return true;
                }
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a port",
                    "the port is not a parameter of the graph, so neither half takes it");
            }
            const size_t index = at->second;
            if (portOf[index] != rt::kInvalidNodeRef) {
                copyOf[ref] = portOf[index];
                return true;
            }
            // A parameter is a pair and this half takes the other component. The forward half does
            // not take tangents. A value the backward half needs is a residual -- a parameter's
            // value is one like any other -- and `makeCopy` takes those out of the tuple before it
            // gets here, so a parameter port that reaches this point is one the cut does not carry.
            if (!tangent) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a tangent port",
                    "the value half reads the tangent of parameter " +
                        std::to_string(parameterOfPort(source->funcType(), index).first) +
                        ", which the forward half does not take");
            }
            return refuseAt(
                refusal,
                source,
                ref,
                "a parameter",
                "the backward half reads the value of parameter " +
                    std::to_string(parameterOfPort(source->funcType(), index).first) +
                    ", and it is not one of the values the cut carries");
        }
        // What the refusal above states is the invariant this cut keeps: the tangent half reads
        // only values that cross the cut -- the ones `residualsOf` listed -- plus what the tangent
        // half itself computes from them. A parameter's value is one of the former whenever the
        // tangent half reads it: `residualsOf` asks the tangent side's own walk what it reads, not
        // which parameter index a port belongs to, and it keeps a parameter's value port on the
        // same terms it keeps a computed value (the rule does not split by type: everything the
        // later half reads is a residual). That is also why the refusal is not
        // reachable today: a parameter value the backward half reads is in the residual list before
        // this walk runs, so `makeCopy` answers it from the tuple (`crossesCut`, above) and never
        // arrives here.
        //
        // It is kept, deliberately, and not replaced by an assertion: 对 > 明确拒绝 > 错数 > 崩 is
        // the order, and an assertion here would turn a refusal into the worst of the four. Its
        // being unreachable in the shapes tried is a reading and not a proof: no case in the tree
        // pins this message, and no shape found so far reaches it. Zero entries and unreachable are
        // two different conclusions, and counting must not stand in for proving. If it is ever
        // *proved* unreachable for every input, delete this refusal and demote the invariant above
        // to a comment -- do not delete it on the strength of a count. Never delete it while it is
        // the only refusal standing between this walk and a half that reads a value nothing
        // carries.
        case NodeCase::Data: {
            // A constant stands for the same value in the copy; `writeConstant` is that writer, so
            // the DATA case and the reachability walk write it the same way. A constant whose slot
            // is not readable has no copy.
            const gc_node_ref_t written = writeConstant(ref);
            if (written == rt::kInvalidNodeRef) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a constant",
                    "the slot of the constant is not readable");
            }
            copyOf[ref] = written;
            return true;
        }
        case NodeCase::Oper: {
            // The inputs arrive as one list in port order -- the ordinary ones and then the context
            // ones (`halfInputsOf` is `dataInputsOf`) -- so they are placed back on the copy the
            // way they were found: a member read's place, an `append`'s value or a `softmax<1>`'s
            // axis handed to `setNormInputs` would be an ordinary operand of a different call.
            const auto *body         = source->nodeBodyAs<rt::GCOperBody>(ref);
            const gc_node_ref_t made = draft.addOperNode(node->dataType(), nullptr, body->uri());
            placeCallArguments(made, ref, inputs);
            copyOf[ref] = made;
            return true;
        }
        case NodeCase::Fill: {
            // The copy keeps the construction's own payload: the slot list is what says which
            // member each value fills, and a call's operands already are that list
            // (`fillPayloadOf`).
            const auto body = fillPayloadOf(*source, ref);
            if (!body) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a construction",
                    "the node builds a value and its payload says neither which slots its values "
                    "fill nor what it hands the template");
            }
            std::vector<size_t> slots;
            slots.reserve(body->slots.size());
            for (const rt::gc_slot_idx_t slot : body->slots) {
                slots.push_back(static_cast<size_t>(slot));
            }
            const std::vector<std::byte> payload = rt::makeFillPayload(body->fillKind, slots);
            const gc_node_ref_t made             = draft.addFillNode(node->dataType(), payload);
            // A fill's data ports are its template (the ordinary one) and the values it
            // carries (the context ones); `inputs` is both, in the source's port order. A
            // construction the source wrote as a call states the same two things in its own
            // operands, template first, so the copy splits at the same place either way: one
            // template, every value after it.
            const size_t normCount = inputs.empty() ? 0 : 1;
            if (normCount > 0) {
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs.data(), normCount));
            }
            if (inputs.size() > normCount) {
                draft.setWithInputs(
                    made,
                    std::span<const gc_node_ref_t>(
                        inputs.data() + normCount,
                        inputs.size() - normCount));
            }
            copyOf[ref] = made;
            return true;
        }
        case NodeCase::Gate: {
            // A gate forwards its **first** input; every input after it is a statement the gate
            // waits on (`camel/runtime/graph.h`). The value is what a reader of the gate reads, and
            // it is what this half copies.
            //
            // The waits are refused here rather than dropped: a half that copied the value and left
            // the waits behind would stop the body's prints from running at all, and one that
            // copied the waits without their place on a chain would run them at a point nobody
            // wrote. Carrying the effect skeleton across the cut is a tier of its own -- until it
            // exists, this is a refusal and not a silent wrong answer (a cut that read the gate's
            // last input as the value would place a print where the result should be).
            //
            // The answer is named and held here, not viewed: `gateWaitsOf` answers with a vector by
            // value (`camel/runtime/graph.h`), so a `std::span` over it would be a view of a
            // temporary destroyed at the end of the declaration -- `.empty()` alone would still
            // read the span's own size and say nothing, and the first reader of an element would
            // read freed memory.
            const std::vector<gc_node_ref_t> waits = gateWaitsOf(*source, ref);
            if (!waits.empty()) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a gate that waits on a statement",
                    "the gate forwards its value and waits on the statements written beside it, "
                    "and "
                    "the cut does not carry a statement across yet: a half would either drop the "
                    "print or run it where it was not written");
            }
            const gc_node_ref_t forwarded = valueBehindGates(*source, ref);
            if (forwarded == rt::kInvalidNodeRef) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a gate",
                    "the gate forwards no value, so there is nothing for this half to read");
            }
            copyOf[ref] = inputs.front();
            return true;
        }
        case NodeCase::Call: {
            // A rule's right side reaches the transform as a call: the operands are its ordinary
            // inputs and the rule's own graph is the function value on its context port (`jvp.cpp`
            // builds exactly that shape). The callee is a graph of the program and is not copied,
            // so the call is the same call with its inputs placed on this half's side of the cut.
            //
            // A call through a value whose **signature answers the pair** is the other shape: the
            // value is a lifted function (it came from `jvp(v)`), so the value half cannot call it
            // -- it takes the tangents too -- and the call it places is `split(v)`, the forward
            // half of whatever cut that value turns out to have: the forward half
            // calls `split(v)`, and the backward half hands the cotangent to the `lin` it returns.
            // Which graph `v` is is asked at run time by the operator, never read here. The answer
            // is `(result, lin)`, and the tangent the source read is the second half's call of that
            // `lin` (`residualsOf` carries it as the call's linear half).
            if (!tangent && answersAPairByValueAt(*source, ref)) {
                camel::core::type::FunctionType *valueType =
                    callValueSignatureOf(source->view(), ref);
                FunctionType *splitType =
                    valueType != nullptr ? splitOperatorType(valueType) : nullptr;
                const size_t withCount = source->withInputsOf(ref).size();
                if (splitType == nullptr || withCount == 0 || withCount > inputs.size()) {
                    return refuseAt(
                        refusal,
                        source,
                        ref,
                        "a call through a function value",
                        "the value's signature has no halves and no `split` of it to call, or the "
                        "call passes no value for the place its callee stands in");
                }
                const size_t values      = inputs.size() - withCount;
                const SplitHalves halves = splitHalvesOf(valueType);
                if (halves.forward == nullptr) {
                    return refuseAt(
                        refusal,
                        source,
                        ref,
                        "a call through a function value",
                        "the value's signature has no forward half, so the call has no value to "
                        "answer with");
                }
                std::vector<gc_node_ref_t> placed = inputs;
                const gc_node_ref_t splitValue =
                    draft.addOperNode(splitType, nullptr, "transforms:split");
                draft.setNormInputs(
                    splitValue,
                    std::span<const gc_node_ref_t>(&placed[values], size_t(1)));
                placed[values]           = splitValue;
                const gc_node_ref_t made = draft.addCallNode(halves.forward->exitType());
                placeCallArguments(made, ref, placed);
                copyOf[ref] = made;
                return true;
            }
            const auto *body         = source->nodeBodyAs<rt::GCCallBody>(ref);
            const gc_node_ref_t made = body != nullptr ? draft.addCallNode(node->dataType(), *body)
                                                       : draft.addCallNode(node->dataType());
            const size_t normCount   = source->normInputsOf(ref).size();
            if (normCount > 0) {
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs.data(), normCount));
            }
            if (inputs.size() > normCount) {
                draft.setWithInputs(
                    made,
                    std::span<const gc_node_ref_t>(
                        inputs.data() + normCount,
                        inputs.size() - normCount));
            }
            copyOf[ref] = made;
            return true;
        }
        case NodeCase::Func: {
            // A call to a named function, which is the shape a rule reaches a half in now that
            // `reinterp` expands a rule's right side at the match site (`jvp.cpp`): what was a call
            // through a function value is a call the graph names. The callee is a graph of the
            // program and not a value on either side of the cut, so the copy names the same graph
            // and the call's inputs are placed on this half's side.
            const GraphId callee = source->calleeIdOf(ref);
            if (!rt::isValid(callee)) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "a call to a named function",
                    "the node names no callee graph, so there is no function to call");
            }
            if (answersAPairAt(*source, program, ref) && !tangent) {
                // The callee answers the pair (value, tangent) by its content (`answersAPair`), so
                // the value half answers the call with **`split`'s forward half of that callee**:
                // the same values in, `(result, lin)` out. `lin` is the callee's own linear half,
                // closed over whatever its body reads, so the call site never sees those values --
                // `residualsOf` names `lin` itself for the backward half to call.
                const auto *calleeGraph = calleeOf(ref);
                CutMember nested;
                if (calleeCuts != nullptr && calleeGraph != nullptr) {
                    const auto known = calleeCuts->find(calleeGraph);
                    if (known != calleeCuts->end()) {
                        nested = known->second;
                    }
                }
                if (nested.answer == nullptr) {
                    return refuseAt(
                        refusal,
                        source,
                        ref,
                        "a call to a function that answers the pair",
                        "the cut of that callee has not been made here, so the value half has no "
                        "forward half of it to call -- a branch or a loop body builds its members "
                        "one "
                        "at a time, and this call is inside one");
                }
                const gc_node_ref_t made = draft.addFuncNode(
                    nested.forwardId,
                    nested.answer,
                    0,
                    draft.allocateRuntimeSlot(nested.answer),
                    0);
                placeCallArguments(made, ref, inputs);
                draft.addDependency(nested.forwardId);
                copyOf[ref] = made;
                return true;
            }
            // A fact about the call, not about the half it is copied into: whatever the rule's
            // expansion recorded on the call it produced, the copy is the same call and says the
            // same thing. It is what `transpose` reads at a call inside an arm. The record is a
            // **set of argument positions** -- one bit per position, bit `i` set when the call's
            // i-th ordinary argument is a tangent (`GCNode::tangentArgs`,
            // `GCGraph::callTangentArgsOf`) -- and not a count of a trailing run: the writer is the
            // rule's own text, and a rule may put a tangent wherever its right side writes one
            // (`mul_tangent(x, w, dx, dw)` ends with its two, `index_into(dxs, i)` begins with
            // one), so what a reader needs is the position and not the total. `jvp` writes the
            // same shape of record when it builds a lifted call, over the positions its tangent
            // half occupies. The count of tangents is therefore derived from the set by whoever
            // wants it (the number of set bits below `kTangentArgumentBits`), never read off the
            // record.
            const gc_node_ref_t made = draft.addFuncNode(
                callee,
                node->dataType(),
                0,
                draft.allocateRuntimeSlot(node->dataType()),
                0,
                source->callTangentArgsOf(ref));
            placeCallArguments(made, ref, inputs);
            draft.addDependency(callee);
            copyOf[ref] = made;
            return true;
        }
        case NodeCase::Accs: {
            // A component of the pair a call answers. On the value half the call in hand answers
            // `(result, lin)`, so component 0 is the value the source's call computed and component
            // 1 is the linear half -- a function value, not the source's tangent. On the backward
            // half a read of component 1 is **the call of that linear half**: one indirect call, on
            // the tangents the source call passed, and the call site never sees what the callee's
            // body reads.
            const std::optional<MemberRead> read   = memberReadOf(*source, ref);
            const std::vector<gc_node_ref_t> reads = dataInputsOf(source->view(), ref);
            if (!read.has_value() || reads.empty()) {
                return refuseAt(
                    refusal,
                    source,
                    ref,
                    "an access node",
                    "the node reads no input, so the half has nothing to project");
            }
            // A member of a **struct** is read by name (`qq.a`, `GCAccsKind::StructKey`) and a
            // member of a tuple by place (`qq.0`, `GCAccsKind::TupleIndex`). The draft writes both
            // forms
            // (`addAccsNode(type, key)` and `addAccsNode(type, index)`), and the copy says
            // whichever one the source said: a keyed read copied as a place would name another
            // member, and on a `Struct` it would index the value as if it were a `Tuple` -- `idx <
            // t->size()`, "Tuple index out of bounds" in `nodevm.cpp` (as in
            // `split(jvp((qq: Point) => qq.a * 4.0 + qq.b))(p)` when the copy drops the key).
            //
            // The name needs no place in either half's port list. A struct is a port like any other
            // -- `Tangent<Point>` is the struct of the members' tangents (design principles 9.9),
            // so the two halves take one `Point` each and read the same member of it by the same
            // name
            // (`jvp.cpp#LiftState` projects a keyed access the same way, with `addAccsNode(type,
            // key)`). What the cut was missing was the name in the copy and not the shape of the
            // ports: `split_struct_field_read` reads `forward y= 11.0` and `lin dp= 4.0`, the two
            // `jvp` alone already answered, and `split_struct_port` -- the same program without the
            // field read -- reads a struct through both halves.
            if (const gc_node_ref_t call = pairTangentReadOf(*source, program, ref);
                call != rt::kInvalidNodeRef) {
                if (!tangent) {
                    // The value half's copy of that call answers `(result, lin)`, and the source's
                    // read of component 1 is a read of the **lifted** linear half -- `Lift<lin>`,
                    // which is what `jvp(lin)` denotes (`Lift<A => B> = Lift<A> => Lift<B>`; the
                    // direction is a parameter of the lifted function, so the second order is fed
                    // twice and never captured).
                    //
                    // That this is the value the source asked for is a reading and not an
                    // assumption: the type the source's read has and `jvpSignature` of the linear
                    // half are the **same type object**, compared by pointer, which is the same
                    // object for `(float, float) => (float, float) pure`. Where they differ, the
                    // read asks for something that is no component's lift, and it is refused below
                    // by name.
                    FunctionType *calleeType = nullptr;
                    if (source->node(call) != nullptr &&
                        source->node(call)->kind == rt::GCNodeKind::Call) {
                        calleeType = callValueSignatureOf(source->view(), call);
                    } else if (
                        const auto *callee = calleeGraphOf(*source, program, call);
                        callee != nullptr) {
                        calleeType = callee->funcType();
                    }
                    const SplitHalves halves = splitHalvesWith(calleeType, nullptr);
                    FunctionType *linType    = halves.backward;
                    FunctionType *liftedType =
                        linType != nullptr && linType->code() == TypeCode::Function
                            ? jvpSignature(linType)
                            : nullptr;
                    if (liftedType == nullptr || node->dataType() != liftedType) {
                        return refuseAt(
                            refusal,
                            source,
                            ref,
                            "a tangent of a call",
                            "the value half hands the call's linear half on lifted, and this read "
                            "is "
                            "not that lifted type: the linear half of this call lifts to `" +
                                (liftedType != nullptr ? liftedType->toString()
                                                       : std::string("no type")) +
                                "`, while the read asks for `" +
                                (node->dataType() != nullptr ? node->dataType()->toString()
                                                             : std::string("no type")) +
                                "`");
                    }
                    // `lin` is the value half's own component 1 of the copied call -- the read of a
                    // value the copy already answers, not a second call to anything -- and the
                    // value handed on is `jvp` of it, typed by the one spelling of that operator's
                    // signature (`liftedOperatorType`).
                    const gc_node_ref_t callCopy = inputs.front();
                    const gc_node_ref_t lin      = draft.addAccsNode(linType, 1);
                    draft.setNormInputs(lin, std::span<const gc_node_ref_t>(&callCopy, size_t(1)));
                    const gc_node_ref_t lifted =
                        draft.addOperNode(liftedOperatorType(linType), nullptr, "transforms:jvp");
                    draft.setNormInputs(lifted, std::span<const gc_node_ref_t>(&lin, size_t(1)));
                    copyOf[ref] = lifted;
                    return true;
                }
                const auto index = residualIndexOf->find(call);
                if (index == residualIndexOf->end()) {
                    return refuseAt(
                        refusal,
                        source,
                        ref,
                        "a linear half",
                        "the cut does not carry the linear half of this call, so the second half "
                        "has "
                        "no function to call");
                }
                const gc_node_ref_t lin = projectResidual(index->second, refusal);
                if (lin == rt::kInvalidNodeRef) {
                    return false;
                }
                const gc_node_ref_t made = draft.addCallNode(node->dataType());
                if (!inputs.empty()) {
                    draft.setNormInputs(
                        made,
                        std::span<const gc_node_ref_t>(inputs.data(), inputs.size()));
                }
                draft.setWithInputs(made, std::span<const gc_node_ref_t>(&lin, 1));
                copyOf[ref] = made;
                return true;
            }
            const gc_node_ref_t made = read->kind == rt::GCAccsKind::StructKey
                                           ? draft.addAccsNode(node->dataType(), read->key)
                                           : draft.addAccsNode(node->dataType(), read->place);
            draft.setNormInputs(made, inputs);
            copyOf[ref] = made;
            return true;
        }
        default:
            return refuseAt(
                refusal,
                source,
                ref,
                "a node of another kind",
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
                const bool refusedAtNode = here != nullptr && (here->kind == rt::GCNodeKind::Loop ||
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
/// numbered them, each of the residual's own type. The order and the count are the graph's answer,
/// so the closure tuple the forward half builds and the ports this declares are read off the same
/// list.
///
/// The draft is a template parameter for the reason the branch helpers give: this file builds
/// through what the transaction hands it, and a parameter written as the type is one more of the
/// direct connections `runtime.graph_draft_ledger` counts, which may only fall.
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

/// The loop a lifted graph's exit is, as the cut needs it: the node, its regions, the pair the body
/// yields, the two halves of the state, and what the backward half reads from a round (the tape)
/// and from outside the loop (the invariants that cross the cut).
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
    /// What the backward half reads from outside the loop: the invariants that cross the cut, then
    /// the values it reads *above* the loop -- the ones computed from the loop's own result, marked
    /// `afterLoop`. One list because both cross the cut
    /// the same way, one closure member each; the flag says where the forward half takes the value,
    /// before the loop or after it.
    std::vector<Residual> outside;
    /// The nodes that read one component of the loop's pair through `GCAccsKind::TupleIndex` and
    /// stand outside the body: a half seeds each of them with its own value for that component (the
    /// forward half's `(state, tape)` component 0, the backward half's tangent state), so the chain
    /// above the loop is copied without the walk ever entering the loop cell. `valueProjections` is
    /// every node that reads component 0, including the ones only the tangent chain reads (they
    /// feed a residual taken after the loop); `tangentProjections` is the component-1 ones, and
    /// only those -- a component-0 projection seeded in the backward half would carry the wrong
    /// value.
    std::vector<gc_node_ref_t> valueProjections;
    std::vector<gc_node_ref_t> tangentProjections;
    Type *stateValueType   = nullptr;
    Type *stateTangentType = nullptr;
    Type *elementType      = nullptr;
    /// The exit as the program wrote it, when the pair the exit answers with is not the loop cell's
    /// own value but a chain above it: the loop is then *inside* the chain the exit reads (for
    /// example `g(s) = (loop x = s ...)[0]` -- the exit is a tuple fill whose two members are
    /// `idx_arr` of the loop's component 0 and component 1). These are the tops of those two
    /// chains, and each half computes its own above the loop it made. `kInvalidNodeRef` on both
    /// when the exit is the cell itself, which is the shape `jvp` gives a loop a function returns
    /// whole.
    gc_node_ref_t exitValueRoot   = rt::kInvalidNodeRef;
    gc_node_ref_t exitTangentRoot = rt::kInvalidNodeRef;
};

/// Reads the loop the exit is, or says at the node why this loop is not the shape the cut handles.
/// Every refusal here names what is missing rather than saying "not supported": a loop that is
/// lifted but not cut is a reading of its own (`skip=loop ... why=`), and the next cell of the cut
/// is one of these lines, not a second entry.
bool readLoopCut(const rt::GCGraph *function, gc_node_ref_t loop, std::ostream &os, LoopCut &cut);

/// The whole cut of a loop: the forward half runs the primal loop and tapes each round, the
/// backward half loops over that tape and hands the tangent on.
SplitGraph cutLoopGraph(
    const camel::core::context::context_ptr_t &context, const rt::Program *program,
    rt::GCGraph *function, std::ostream &os, LoopCut &cut);

} // namespace camel::transforms
