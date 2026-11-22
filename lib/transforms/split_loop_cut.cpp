// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A loop's cut: reading the loop the exit of a lifted graph is, and building the two loops that cut
 * it. This is the loop tier of `split` -- `split.cpp` keeps the straight-line and branch tiers and
 * the entry point, and asks `readLoopCut` and `cutLoopGraph` from here.
 */

#include "split_internal.h"
#include "refusal_readings.h"

namespace camel::transforms {

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
//     closure like every other residual.
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
    const auto insideRegion                    = [graph, region, ports](gc_node_ref_t ref) {
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
bool readLoopCut(const rt::GCGraph *function, gc_node_ref_t loop, std::ostream &os, LoopCut &cut) {
    const std::string name = function->name();
    const auto refuse      = [&](std::string_view what) {
        os << "REINTERP split function=" << name << " skip=" << refusalCauseName(RefusalCause::Loop)
           << " (" << what << ")\n";
        noteRefusal(RefusalReading("split", name, RefusalCause::Loop));
        return false;
    };
    cut.loop = loop;
    cut.body = rt::loopBodyOf(*function, loop);
    // The iterable form does not decide whether a loop can be cut: `jvp` lifts both forms
    // . Only the element type
    // differs, and it is read from the tail's own form.
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
    cut.elementPort             = ports[0];
    cut.statePort               = ports[1];
    const rt::GCNode *stateNode = function->node(cut.statePort);
    if (stateNode == nullptr || stateNode->dataType() == nullptr ||
        stateNode->dataType()->code() != TypeCode::Tuple) {
        return refuse(
            "the loop's state port is not the pair `jvp` writes, so the loop is not lifted");
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
    const gc_node_ref_t result = bodyRegion != nullptr && bodyRegion->result < function->nodeCount()
                                     ? bodyRegion->result
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
        const rt::GCNode *node                     = function->node(ref);
        const auto *body                           = function->nodeBodyAs<rt::GCAccsBody>(ref);
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
    // The parameter a graph port is the *value* of, or `kNotAParameter`. A port at a tangent
    // position is the half's own parameter, not a value anything carries, and a port that is no
    // parameter's (a seeded value, a round port) answers `kNotAParameter` too.
    const std::span<const gc_node_ref_t> graphPorts = function->normPorts();
    const auto parameterValueOf                     = [&](gc_node_ref_t ref) -> size_t {
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
    // **A parameter's own value is a residual when the second half reads it.** The branch cut
    // carries every value parameter -- its arms may read any of them -- and the loop carries the
    // ones its second half actually reads, which is a fact of the walk rather than of the
    // signature: `int_escape`'s half reads `x` through `3.0*x`, the truncation and `float(...)`
    // even though the round computes none of those, and a signature cannot say so.
    //
    // The walk is the one the copy makes, with the round's own leaves removed: it descends through
    // the values the cut carries as a whole (they are on this side of the cut anyway) and stops at
    // a port whose value the round itself owns -- the state, the round's element -- because the
    // tape carries those and their inputs are not the half's. A constant is skipped for the reason
    // above.
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
        invariants.begin(),
        invariants.end(),
        [&parameterValueOf](gc_node_ref_t a, gc_node_ref_t b) {
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
    // **A residual computed from the loop's own result is taken after the loop**. `jvp_idx_arr`
    // states the length of the array it reads (`pick_at(dxs, i, len(xs))`) instead of letting the
    // transposed rule read a linear operand, so the tangent side of an escape reads `len(xs)` where
    // `xs` is the loop's *value*: a node above the cell, which exists only once the forward half's
    // loop has run. Nothing before the loop can compute it, so it is not an invariant.
    //
    // What is taken is the smallest set the forward half can finish: every node above the cell that
    // reads one of the cell's value projections, and nothing below it (a node reading such a node
    // is copied on top of the residual by the half that needs it). The descent stops there, so the
    // walk never enters the cell.
    if (cut.exitTangentRoot != rt::kInvalidNodeRef) {
        std::vector<gc_node_ref_t> above;
        std::vector<gc_node_ref_t> stack{cut.exitTangentRoot};
        std::unordered_set<gc_node_ref_t> visited;
        while (!stack.empty()) {
            const gc_node_ref_t ref = stack.back();
            stack.pop_back();
            if (ref >= function->nodeCount() || ref == cut.loop || !visited.insert(ref).second) {
                continue;
            }
            const rt::GCNode *node = function->node(ref);
            if (node == nullptr || node->kind == rt::GCNodeKind::Data) {
                continue;
            }
            const std::vector<gc_node_ref_t> inputs = dataInputsOf(function, ref);
            const bool readsTheCellsValue =
                std::any_of(inputs.begin(), inputs.end(), [&cut](gc_node_ref_t input) {
                    return std::find(
                               cut.valueProjections.begin(),
                               cut.valueProjections.end(),
                               input) != cut.valueProjections.end();
                });
            if (readsTheCellsValue) {
                above.push_back(ref);
                continue;
            }
            for (gc_node_ref_t input : inputs) {
                stack.push_back(input);
            }
        }
        std::sort(above.begin(), above.end());
        for (gc_node_ref_t ref : above) {
            const rt::GCNode *node = function->node(ref);
            if (node == nullptr || node->dataType() == nullptr) {
                return refuse(
                    "a value the backward half computes from the loop's result has no type");
            }
            Residual one;
            one.node      = ref;
            one.parameter = Residual::kNotAParameter;
            one.type      = node->dataType();
            one.afterLoop = true;
            cut.outside.push_back(one);
        }
    }
    return true;
}

/// The whole cut of a loop: the forward half runs the primal loop and tapes each round, the
/// backward half loops over that tape and hands the tangent on. Both halves keep the source's round
/// count by construction -- the tape has one element per round the forward half ran.
SplitGraph cutLoopGraph(
    const camel::core::context::context_ptr_t &context, const rt::Program *program,
    rt::GCGraph *function, std::ostream &os, LoopCut &cut) {
    SplitGraph built;
    if (!readLoopCut(function, cut.loop, os, cut)) {
        return built;
    }
    const std::span<const gc_node_ref_t> sourcePorts = function->normPorts();
    // The tape's element type: one tuple per round. A body whose tangent half reads nothing from
    // the round still has to say how many rounds ran (design principles 9.9:
    // 循环体回传不读残差时仍收一个 常量 1，反向循环靠它知道轮数), so the tape is then one `1` per
    // round.
    std::vector<Type *> tapeMemberTypes;
    tapeMemberTypes.reserve(cut.tape.size() + 1);
    for (gc_node_ref_t ref : cut.tape) {
        const rt::GCNode *node = function->node(ref);
        if (node == nullptr || node->dataType() == nullptr) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Tape)
               << " (a value the backward half reads from a round has no type)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Tape));
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
    // What crosses the cut: the invariants the backward half reads from outside the loop, the
    // values it reads from the loop's own result (taken after the loop, `afterLoop`), then the
    // tape. That order is the closure's: invariants, after-loop values, tape.
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
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Signature) << " (" << halves.why << ")\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Signature));
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
           << " skip=" << refusalCauseName(RefusalCause::Loop) << " (the loop takes "
           << loopInputs.size()
           << " inputs, and a `for` loop with one state takes the array and its first state)\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Loop));
        return built;
    }
    const gc_node_ref_t iterable                       = loopInputs[0];
    const gc_node_ref_t initialState                   = loopInputs[1];
    const std::span<const gc_node_ref_t> initialHalves = function->withInputsOf(initialState);
    if (initialHalves.size() != 2) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Loop)
           << " (the loop's first state is not the pair `jvp` writes, so the loop is not "
              "lifted)\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Loop));
        return built;
    }
    // The chain above the cell answers the half's own result: the forward half's value on the value
    // side, the backward half's tangent on the tangent side. Read off the source graph before
    // either half exists -- a chain that answers something else is not the pair this cut splits,
    // and saying so here keeps the refusal about the shape rather than about a type a copy happened
    // to reach.
    if (cut.exitValueRoot != rt::kInvalidNodeRef || cut.exitTangentRoot != rt::kInvalidNodeRef) {
        const rt::GCNode *valueTop =
            cut.exitValueRoot < function->nodeCount() ? function->node(cut.exitValueRoot) : nullptr;
        const rt::GCNode *tangentTop = cut.exitTangentRoot < function->nodeCount()
                                           ? function->node(cut.exitTangentRoot)
                                           : nullptr;
        const auto *forwardPair      = halves.forward->exitType() != nullptr &&
                                               halves.forward->exitType()->code() == TypeCode::Tuple
                                           ? tt::as_ptr<TupleType>(halves.forward->exitType())
                                           : nullptr;
        if (valueTop == nullptr || tangentTop == nullptr || forwardPair == nullptr ||
            forwardPair->size() != 2 || valueTop->dataType() != forwardPair->typeAt(0) ||
            tangentTop->dataType() != halves.backward->exitType()) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Loop)
               << " (the chain the exit reads above the loop does not answer the halves' "
                  "results)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Loop));
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
        // The invariants are computed before the loop, in region 0: they are one value each, not
        // one per round.
        for (const Residual &one : cut.outside) {
            if (one.afterLoop) {
                // Computed from the loop this half is about to build, so there is nothing to copy
                // yet: it is taken below, once the loop and its projections are in place.
                continue;
            }
            if (!forward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
        }
        // The primal loop: the same rounds, the value state alone, and one collection slot.
        rt::GCLoopBody forwardBody  = cut.body;
        forwardBody.collectionCount = 1;
        Type *forwardLoopType =
            TupleType::create(std::vector<Type *>{cut.stateValueType, tapeType});
        const gc_node_ref_t forwardLoop = draft.addLoopNode(forwardBody, forwardLoopType);
        const uint32_t region = draft.addRegion(rt::GCRegionKind::LoopBody, 0, forwardLoop, 1);
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
               << " skip=" << refusalCauseName(RefusalCause::Forward)
               << " construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                            .atConstruct(built.refusedConstruct));
            return built;
        }
        std::vector<gc_node_ref_t> tapeMembers;
        tapeMembers.reserve(cut.tape.size() + 1);
        if (countingOnly) {
            tapeMembers.push_back(draft.materializeStaticValue(toSlot<int64_t>(1), Type::Int64()));
        } else {
            for (gc_node_ref_t ref : cut.tape) {
                tapeMembers.push_back(forward.copyOf[ref]);
            }
        }
        const gc_node_ref_t tapeTuple = fillTuple(draft, tapeElementType, tapeMembers);
        const std::array<gc_node_ref_t, 2> yielder{forward.copyOf[cut.valueRoot], tapeTuple};
        const gc_node_ref_t bodyResult = fillTuple(
            draft,
            TupleType::create(std::vector<Type *>{cut.stateValueType, tapeElementType}),
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
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
            firstState = forward.copyOf[half];
        }
        if (!forward.copyHalf(iterable, built)) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Forward)
               << " construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                            .atConstruct(built.refusedConstruct));
            return built;
        }
        const std::array<gc_node_ref_t, 2> forwardInputs{forward.copyOf[iterable], firstState};
        draft.setNormInputs(
            forwardLoop,
            std::span<const gc_node_ref_t>(forwardInputs.data(), forwardInputs.size()));
        const gc_node_ref_t forwardRound =
            draft.addPortNode(Type::Int64(), draft.allocateRuntimeSlot(Type::Int64()));
        const std::array<gc_node_ref_t, 2> forwardWith{bodyResult, forwardRound};
        draft.setWithInputs(
            forwardLoop,
            std::span<const gc_node_ref_t>(forwardWith.data(), forwardWith.size()));
        // The two components of the loop's value: the state the program sees, and the tape.
        const gc_node_ref_t value = draft.addAccsNode(cut.stateValueType, 0);
        draft.setNormInputs(value, std::span<const gc_node_ref_t>(&forwardLoop, 1));
        const gc_node_ref_t tapeValue = draft.addAccsNode(tapeType, 1);
        draft.setNormInputs(tapeValue, std::span<const gc_node_ref_t>(&forwardLoop, 1));
        // The exit the program wrote may read the loop's value through a chain above the cell
        // (`g(s) = (loop ...)[0]`): then the half copies that chain, with the cell's value seeded
        // at its foot, and answers with the top of it rather than with the state whole. The
        // projections are seeded for the *whole* cell's pair, so the walk never reaches the loop
        // node and no body region is copied -- what crosses from the cell into the chain is the one
        // value the half has for that component, and nothing else.
        //
        // The seeding is done here, before anything above the loop is copied and whatever the exit
        // is: a residual taken *after* the loop reads one of these projections, so its copy needs
        // them in place even when the program's own exit is the cell itself.
        for (gc_node_ref_t projection : cut.valueProjections) {
            forward.seed(projection, value);
        }
        // The residuals computed from the loop's result: copied once, here -- after the loop, after
        // its projections, and outside the body region, so one value for the whole loop and never
        // one per round. A residual whose read sat inside the body region could not be taken here
        // at all, and none can be: `readLoopCut` finds these on the exit chain above the cell,
        // while a read inside the region reads the round's element or the state, which the tape
        // carries.
        for (const Residual &one : cut.outside) {
            if (!one.afterLoop) {
                continue;
            }
            if (!forward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
        }
        gc_node_ref_t answered = value;
        if (cut.exitValueRoot != rt::kInvalidNodeRef) {
            if (!forward.copyHalf(cut.exitValueRoot, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                                .atConstruct(built.refusedConstruct));
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
            draft,
            program,
            backwardId,
            halves.closure,
            halves.backward,
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{answered, linValue};
        const gc_node_ref_t out = fillTuple(
            draft,
            halves.forward->exitType(),
            std::span<const gc_node_ref_t>(exitHalves.data(), exitHalves.size()));
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the tangents in, the tape out of the closure, and a loop over the
        // tape.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(halves.backward);
        Half backward(draft, function, program, true, &cut.outside, &residualIndexOf);
        addResidualCaptures(draft, cuts, backward);
        for (size_t i = sourcePorts.size() / 2; i < sourcePorts.size(); ++i) {
            Type *type = function->funcType()->normTypeAt(i);
            if (type == nullptr || !camel::core::tangentSpaceOf(type).hasSpace()) {
                // A () slot is an answer this half gives, never an argument it takes.
                continue;
            }
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            backward.portOf[i] = port;
        }
        for (const Residual &one : cut.outside) {
            // An `afterLoop` residual is one of these as well, and it is read the same way --
            // `crossesCut` answers it with its closure port. But it is not walked from here: what
            // it is computed from is the loop cell, which this half cannot copy, and it sits above
            // the exit chain that reads it, so the walk that needs it takes the port. Nothing is
            // recomputed on this side of the cut, and no node is made for a value this half only
            // reads.
            if (one.afterLoop) {
                continue;
            }
            if (!backward.copyHalf(one.node, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Backward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
        }
        const gc_node_ref_t tapePort =
            backward.closurePorts.empty() ? rt::kInvalidNodeRef : backward.closurePorts.back();
        if (tapePort == rt::kInvalidNodeRef) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Backward)
               << " (the tape has no closure port to be read from)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward));
            return built;
        }
        rt::GCLoopBody backwardBody  = cut.body;
        backwardBody.collectionCount = 0;
        // The tape is an array, whatever the primal loop iterated: the backward half walks one
        // element per round the forward half taped, so it is not the interval's pair.
        backwardBody.iterableForm        = camel::core::LoopIterableForm::Array;
        const gc_node_ref_t backwardLoop = draft.addLoopNode(backwardBody, cut.stateTangentType);
        const uint32_t region = draft.addRegion(rt::GCRegionKind::LoopBody, 0, backwardLoop, 1);
        draft.setInsertRegion(region);
        const gc_node_ref_t tapeElement =
            draft.addPortNode(tapeElementType, draft.allocateRuntimeSlot(tapeElementType));
        const gc_node_ref_t state = draft.addPortNode(
            cut.stateTangentType,
            draft.allocateRuntimeSlot(cut.stateTangentType));
        std::vector<gc_node_ref_t> regionPorts{tapeElement, state};
        // The round's residuals, read out of the tape's element by position, and the tangent state.
        for (size_t i = 0; i < cut.tape.size(); ++i) {
            Type *type               = function->node(cut.tape[i])->dataType();
            const gc_node_ref_t part = draft.addAccsNode(type, static_cast<uint32_t>(i));
            draft.setNormInputs(part, std::span<const gc_node_ref_t>(&tapeElement, 1));
            backward.seed(cut.tape[i], part);
        }
        backward.seed(cut.stateTangent, state);
        if (!backward.copyHalf(cut.tangentRoot, built)) {
            draft.setInsertRegion(0);
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Backward)
               << " construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward)
                            .atConstruct(built.refusedConstruct));
            return built;
        }
        draft.setInsertRegion(0);
        draft.setRegionPorts(region, regionPorts);
        draft.setRegionResult(region, backward.copyOf[cut.tangentRoot]);
        // The loop's inputs: the tape it iterates, then the first tangent -- the tangent half of
        // the pair `jvp` made for the first state.
        gc_node_ref_t firstTangent = rt::kInvalidNodeRef;
        {
            const gc_node_ref_t half = initialHalves[1];
            if (!backward.copyHalf(half, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Backward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
            firstTangent = backward.copyOf[half];
        }
        const std::array<gc_node_ref_t, 2> backwardInputs{tapePort, firstTangent};
        draft.setNormInputs(
            backwardLoop,
            std::span<const gc_node_ref_t>(backwardInputs.data(), backwardInputs.size()));
        const gc_node_ref_t backwardRound =
            draft.addPortNode(Type::Int64(), draft.allocateRuntimeSlot(Type::Int64()));
        const std::array<gc_node_ref_t, 2> backwardWith{
            backward.copyOf[cut.tangentRoot],
            backwardRound};
        draft.setWithInputs(
            backwardLoop,
            std::span<const gc_node_ref_t>(backwardWith.data(), backwardWith.size()));
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
                   << " skip=" << refusalCauseName(RefusalCause::Backward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward)
                                .atConstruct(built.refusedConstruct));
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
    // `invariants` counts what the forward half takes *before* the loop -- the reading this cell
    // has always printed -- and `after` what it takes from the loop's result, printed only when
    // there is such a value, so that a cut that has none reads exactly as it did.
    size_t takenAfterTheLoop = 0;
    for (const Residual &one : cut.outside) {
        takenAfterTheLoop += one.afterLoop ? 1 : 0;
    }
    os << "REINTERP split function=" << function->name() << " forward=" << rt::toValue(forwardId)
       << " backward=" << rt::toValue(backwardId) << " type="
       << (forwardMade != nullptr && forwardMade->funcType() != nullptr
               ? forwardMade->funcType()->toString()
               : std::string("-"))
       << " residuals=" << built.residuals << " loop=1 tape=" << cut.tape.size()
       << " invariants=" << (cut.outside.size() - takenAfterTheLoop);
    if (takenAfterTheLoop != 0) {
        os << " after=" << takenAfterTheLoop;
    }
    if (function->recomputesOnBackward()) {
        os << " recompute=1";
    }
    os << '\n';
    built.program  = rt::ProgramHold(*context, group.program());
    built.forward  = const_cast<rt::GCGraph *>(forwardMade);
    built.backward = const_cast<rt::GCGraph *>(backwardMade);
    return built;
}

} // namespace camel::transforms
