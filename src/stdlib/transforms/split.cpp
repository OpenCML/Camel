// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "split_internal.h"
#include "node_operator.h"
#include "refusal_readings.h"

namespace camel::transforms {

/// A refusal, said at the node of the source graph that caused it: the construct the plumbing does
/// not copy, and why. A build that answered with a graph anyway would be a graph that computes
/// something else (design principles 9.9).
bool refuseAt(
    SplitGraph &refusal, const rt::GCGraph *source, gc_node_ref_t ref, std::string construct,
    std::string why) {
    refusal.refusedOrigin = ref < source->nodeCount()
                                ? static_cast<camel::source::origin_id_t>(source->nodeOrigin(ref))
                                : camel::source::kInvalidOriginId;
    refusal.refusedConstruct = std::move(construct);
    refusal.refusedWhy       = std::move(why);
    return false;
}

/// The condition of the branch whose Join is `join`, or `kInvalidNodeRef` when no branch names it:
/// a `Brch` node holds the Join it fills and takes the condition as its input, so the condition of
/// the branch is read off the graph and never guessed from the shape of the arms.
gc_node_ref_t branchConditionOf(const rt::GCGraph *graph, gc_node_ref_t join);

/// The `Brch` node that names `join` as its Join, or `kInvalidNodeRef`: a branch owns the arm
/// regions, so a walk that wants to enter an arm starts here.
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
/// nodes live in it, and how many form ports it takes. This is the entrance a branch cut needs --
/// an arm is a region, and a walk that follows `dataInputsOf` never reaches it -- so these numbers
/// are what a cut can say about an arm before it knows how to build one: the arm is not empty, and
/// it is entered through its form ports rather than through a data input of the branch node.
struct RegionWalk {
    uint32_t region    = 0;
    uint32_t arm       = 0;
    size_t members     = 0;
    size_t ports       = 0;
    size_t nestedBrchs = 0; // branches inside the region: a walk that enters one meets another
    size_t outside = 0; // distinct nodes it reads from outside itself -- what the cut must carry
};

RegionWalk walkRegion(const rt::GCGraph *graph, uint32_t regionId) {
    RegionWalk walk;
    walk.region                = regionId;
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
///
/// The walk itself is `camel/runtime/graph.h#valueBehindGates`, and it reads the gate's **first**
/// input: a gate is `GATE(value, writes...)`, so a walk that took the last input would read the
/// last *write* as the value in any body that printed something. The reading is one function in one
/// header so `split`, `transpose` and `vmap` cannot disagree.
gc_node_ref_t ungated(const rt::GCGraph *graph, gc_node_ref_t ref) {
    return valueBehindGates(*graph, ref);
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

/// The **lifted signature** a function value stands for, read from its type alone: a value of a
/// lifted type is the lifted function itself, and a value of `(f: F) => Lift<F>` -- the type of the
/// `jvp(v)` node a lift writes for a call through a value (`jvp.cpp#liftedOperatorType`) -- is the
/// operator whose **answer** is that lifted function. Both answers come from types; nothing is read
/// about which graph the value is, because that is not known until the program runs.
camel::core::type::FunctionType *liftedSignatureOf(const camel::core::type::Type *type) {
    auto *self = const_cast<camel::core::type::FunctionType *>(
        tt::as_ptr<camel::core::type::FunctionType>(const_cast<camel::core::type::Type *>(type)));
    if (self == nullptr) {
        return nullptr;
    }
    if (splitHalvesOf(self).pair != nullptr) {
        return self;
    }
    if (self->normTypesCount() != 1 || !self->hasExitType()) {
        return nullptr;
    }
    auto *answer = tt::as_ptr<camel::core::type::FunctionType>(self->exitType());
    if (answer == nullptr || splitHalvesOf(answer).pair == nullptr) {
        return nullptr;
    }
    return answer;
}

/// The **type** of the function value a call through a value names. A `Call` node's callee is its
/// first `with` input (`nodevm.cpp#NodeVM::call` reads `withInputs().front()`), and that value's
/// type is the only thing a cut can read about it: which graph it stands for is answered when the
/// program runs, so the halves name the signature and call the transform that answers the graph --
/// `split(v)` -- instead of reading it.
camel::core::type::FunctionType *callValueSignatureOf(const rt::GCGraph &graph, gc_node_ref_t ref) {
    if (ref >= graph.nodeCount() || graph.node(ref) == nullptr ||
        graph.node(ref)->kind != rt::GCNodeKind::Call) {
        return nullptr;
    }
    const std::span<const gc_node_ref_t> with = graph.withInputsOf(ref);
    if (with.empty()) {
        return nullptr;
    }
    const rt::GCNode *value = graph.node(with.front());
    if (value == nullptr) {
        return nullptr;
    }
    return liftedSignatureOf(value->dataType());
}

/// The **value** ports of a lifted signature, read from the type by the law the lift is written
/// with (`jvp.cpp#liftSignature`): `Lift<F>` is `F`'s parameters followed by each one's tangent, in
/// the same order, and a parameter with no tangent space (`n: int`) stands in the tangent half as
/// `()`. So a parameter list is a lift exactly when every port is followed, `half` places on, by
/// **its own tangent space** -- and that is what this asks of each pair of ports, using the type
/// system's one answer for what a tangent is (`tangentSpaceOf`). Nothing here decides by a count
/// alone: a signature whose second half is not the first half's tangents is not a lift, answers no
/// value ports, and then no reader splits its argument list (`Half::halfInputsOf`).
std::vector<size_t> valuePortsOf(const camel::core::type::FunctionType *liftedType) {
    std::vector<size_t> values;
    if (liftedType == nullptr) {
        return values;
    }
    const size_t count = liftedType->normTypesCount();
    if (count == 0 || count % 2 != 0) {
        return values;
    }
    const size_t half = count / 2;
    values.reserve(half);
    for (size_t i = 0; i < half; ++i) {
        const Type *value   = liftedType->normTypeAt(i);
        const Type *tangent = liftedType->normTypeAt(half + i);
        if (value == nullptr || tangent == nullptr) {
            return {};
        }
        const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(value);
        if (space.hasSpace()) {
            if (tangent != space.space) {
                return {};
            }
            values.push_back(i);
            continue;
        }
        // A parameter with no tangent space takes the `()` slot: the tangent half keeps its place
        // so that the halves line up position by position.
        const auto *slot = tt::as_ptr<TupleType>(const_cast<Type *>(tangent));
        if (slot == nullptr || slot->size() != 0) {
            return {};
        }
        values.push_back(i);
    }
    return values;
}

/// The type of `split(f)` for a function of this type: `(f: F) => forward`, built from the **same
/// law** the operator's resolver asks (`split_operator.cpp#splitOperatorSignature`), so a
/// `split(v)` a cut writes is typed exactly as a `split(v)` a program writes is.
FunctionType *splitOperatorType(FunctionType *function) {
    const SplitHalves halves = splitHalvesOf(function);
    if (halves.pair == nullptr) {
        return nullptr;
    }
    camel::core::type::FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(const_cast<Type *>(halves.pair));
    return factory.build();
}

/// Which parameter a lifted port belongs to, and whether it is that parameter's tangent. A port
/// that is no parameter's (a seeded value, a residual) answers `npos` and false.
///
/// One mapping, written once: the lift's parameters are `count / 2` values followed by `count / 2`
/// tangents, so port `k < half` is parameter `k`'s value and port `half + k` is its tangent. Every
/// reader of that column walks the same list, and the halves are the same length by construction
/// (a `()` slot for a parameter without a tangent space), so no reader has to skip anything.
std::pair<size_t, bool>
parameterOfPort(const camel::core::type::FunctionType *liftedType, size_t port) {
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

/// The two halves of the **answer**, read member by member from the lifted type
/// (`liftedShapeOf` / `liftedMemberOf`, `lift.h`) instead of from two places in the exit's node.
///
/// A lifted answer has two forms, and the type says which one it is:
///
///   - `Lift<R>` is the pair `(R, Tangent<R>)` -- the ordinary case. The exit's two members are the
///     halves as they stand: the forward half's answer and the backward half's.
///   - `Lift<R>` is a tuple of member lifts -- an answer that holds a function. Each member is read
///     back into the source member it came from: a **value** member is itself a pair, and gives its
///     value leg to the forward half and its tangent leg to the backward half; a **function**
///     member gives the lifted function to the forward half and nothing to the backward half, since
///     a function has no tangent space (design principles 9.9).
///
/// Reading two positions instead of this law is what made a cut of a lifted graph walk into a
/// tangent its own value half cannot have: in the second form the answer's first member is itself a
/// pair, so handing it to the forward half as it stands hands it both legs.
struct ExitHalves {
    /// The nodes the forward half copies, in the answer's member order.
    std::vector<gc_node_ref_t> forward;
    /// The nodes the backward half copies, one per **value** member, in the same order.
    std::vector<gc_node_ref_t> tangent;
    /// Why the answer is not a form `Lift` writes; empty when it is.
    std::string why;
    bool ok() const { return why.empty(); }
};

ExitHalves exitHalvesOf(const rt::GCGraph &graph, gc_node_ref_t exit, const Type *exitType) {
    ExitHalves halves;
    if (exit >= graph.nodeCount() || graph.node(exit) == nullptr || exitType == nullptr) {
        halves.why = "the exit is not a node with a type";
        return halves;
    }
    const std::span<const gc_node_ref_t> members = graph.withInputsOf(exit);
    const LiftedShape shape = liftedShapeOf(const_cast<Type *>(exitType));
    if (shape.pair) {
        if (members.size() != 2) {
            halves.why =
                "the answer is the pair (value, tangent), and the exit does not hold two members";
            return halves;
        }
        halves.forward.push_back(ungated(&graph, members[0]));
        halves.tangent.push_back(ungated(&graph, members[1]));
        return halves;
    }
    if (shape.type == nullptr) {
        halves.why = "the answer is not a shape `Lift` writes";
        return halves;
    }
    for (gc_node_ref_t member : members) {
        const rt::GCNode *made = graph.node(member);
        const LiftedMember read =
            liftedMemberOf(made != nullptr ? const_cast<Type *>(made->dataType()) : nullptr);
        if (read.forward == nullptr) {
            halves.why = "a member of the answer is not a form `Lift` writes: it is neither a "
                         "(value, tangent) pair nor a lifted function";
            return halves;
        }
        if (!read.value) {
            // A function member: the forward half keeps the lifted function, the backward half
            // takes nothing for it.
            halves.forward.push_back(ungated(&graph, member));
            continue;
        }
        const std::span<const gc_node_ref_t> legs = graph.withInputsOf(member);
        if (legs.size() != 2) {
            halves.why = "a value member of the answer is a pair, and its fill does not hold the "
                         "value and its tangent";
            return halves;
        }
        halves.forward.push_back(ungated(&graph, legs[0]));
        halves.tangent.push_back(ungated(&graph, legs[1]));
    }
    return halves;
}

/// The graph a call node names, or null when it names none (or the program does not have it).
const rt::GCGraph *
calleeGraphOf(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    if (program == nullptr) {
        return nullptr;
    }
    const GraphId callee = graph.calleeIdOf(ref);
    return rt::isValid(callee) ? program->graph(callee) : nullptr;
}

/// Whether the function a **call through a value** names answers the pair, asked of its **type**:
/// the same question `answersAPair` asks of a callee graph, asked of the signature where there is
/// no graph to ask yet. Which graph the value turns out to be is asked at run time by `split(v)`
/// itself, which refuses there, by name, when the graph does not answer a pair.
bool answersAPairByValueAt(const rt::GCGraph &graph, gc_node_ref_t ref) {
    camel::core::type::FunctionType *valueType = callValueSignatureOf(graph, ref);
    if (valueType == nullptr) {
        return false;
    }
    return splitHalvesOf(valueType).pair != nullptr;
}

/// Whether the call `ref` names a function that answers the pair (`answersAPair`) -- by a callee
/// graph it names, or, for a call through a value, by the value's signature
/// (`answersAPairByValueAt`). This is the one thing the cut needs to know about a callee, and it is
/// a question about **content**, not about where the graph came from: a call to such a callee is
/// answered, on the value half, by `split`'s forward half of that callee.
bool answersAPairAt(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    if (ref >= graph.nodeCount() || graph.node(ref) == nullptr) {
        return false;
    }
    if (graph.node(ref)->kind == rt::GCNodeKind::Call) {
        return answersAPairByValueAt(graph, ref);
    }
    if (graph.node(ref)->kind != rt::GCNodeKind::Func) {
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
/// backward half one port per tangent that has a space and skips the rest. So the slot's argument
/// is dropped here, by the callee's own signature -- the same list, read from the same place
/// (`parameterOfPort`), so a call site and the graph it calls number their ports alike.
///
/// Handing it on was one argument too many, and the run time says so by name:
/// `Runtime indirect call norm-arity mismatch: caller graph 'split_of_jvp_of_g_backward' node 6 has
/// 2 args, target graph 'split_of_jvp_of_widen_backward' has 1 norm ports`, on a callee
/// `widen(n: int, x: float)` whose lifted list is `(int, float, (), float)`.
std::vector<gc_node_ref_t>
tangentArgumentsOf(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    const std::vector<gc_node_ref_t> all = dataInputsOf(&graph, ref);
    const size_t withCount               = graph.withInputsOf(ref).size();
    const size_t ordinary                = all.size() > withCount ? all.size() - withCount : 0;
    const size_t half                    = ordinary / 2;
    const auto *callee                   = calleeGraphOf(graph, program, ref);
    const FunctionType *calleeType =
        callee != nullptr ? callee->funcType() : callValueSignatureOf(graph, ref);
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

/// The call whose pair's **tangent** component `ref` reads, when `ref` is that read: an access node
/// of the tuple kind taking component 1 of a call to a function that answers a pair. Component 0 is
/// not one of these -- the value the call computes is a value like any other, and the forward half
/// has it as the first component of what its own call answered.
gc_node_ref_t
pairTangentReadOf(const rt::GCGraph &graph, const rt::Program *program, gc_node_ref_t ref) {
    if (nodeOperatorOf(graph, ref) != NodeOperator::Accs) {
        return rt::kInvalidNodeRef;
    }
    const std::optional<MemberRead> read = memberReadOf(graph, ref);
    if (!read.has_value() || read->kind != rt::GCAccsKind::TupleIndex || read->place != 1) {
        return rt::kInvalidNodeRef;
    }
    const std::vector<gc_node_ref_t> reads = dataInputsOf(&graph, ref);
    // The value read is the node's last data port: a read written as a call states the place
    // first (`access: (Int64, T) => ...`), and a node of the ACCS kind has no other port.
    if (reads.empty() || !answersAPairAt(graph, program, reads.back())) {
        return rt::kInvalidNodeRef;
    }
    return reads.back();
}


/// `lin`: the backward graph, closed over the residuals the forward half computed.
///
/// A FILL of the `FunctionClosure` kind -- the kind a rule's right side already builds for its
/// closure (`rule_expand.cpp` reads one back): the static input is a function value naming the
/// backward graph, whose closure layout is the one this build read from that graph, and the `with`
/// inputs are the residual values in slot order. The kernel builds `lin` once it has
/// the graph: the values do not exist before the forward half
/// runs, so `lin` is built where they do, and what it captures is stated by no signature.
///
/// Here because *two* builds write this shape, not one: the cut hands `lin` out of the forward
/// half, and a call site that transposes a pair-answering callee closes the same graph over the
/// same values
/// (`transpose/walk.cpp`, `transposePairCall`). One writer, so a closure a cut made and a closure a call
/// site made are the same value.
gc_node_ref_t buildLinearClosure(
    rt::GraphDraft &draft, const rt::Program *program, GraphId backwardId,
    const camel::core::type::TupleType *closure, FunctionType *linType,
    std::span<const gc_node_ref_t> residuals) {
    auto *lin                  = ::Function::create(program, backwardId, closure, mm::autoSpace());
    const gc_node_ref_t source = draft.materializeStaticValue(toSlot<::Function *>(lin), linType);
    std::vector<size_t> slots(residuals.size());
    std::iota(slots.begin(), slots.end(), size_t{0});
    const gc_node_ref_t value =
        draft.addFillNode(linType, rt::makeFillPayload(rt::GCFillKind::FunctionClosure, slots));
    draft.setNormInputs(value, std::span<const gc_node_ref_t>(&source, 1));
    draft.setWithInputs(value, residuals);
    return value;
}

std::vector<Residual> residualsOf(
    const rt::GCGraph &lifted, const rt::Program *program, std::string *why) {
    std::vector<Residual> residuals;
    if (lifted.funcType() == nullptr) {
        return residuals;
    }
    // The exit's two sides, read from the lifted type by the one reader of that law
    // (`exitHalvesOf`): the answer is the pair (value, tangent), or a tuple of member lifts whose
    // value members give their two legs to the two halves and whose function member belongs to the
    // forward half alone. A graph whose answer is not a shape `Lift` writes is not a lifted graph,
    // and there is no cut to compute residuals of.
    const gc_node_ref_t exit = ungated(&lifted, lifted.exitNodeRef());
    if (nodeOperatorOf(lifted, exit) != NodeOperator::Fill) {
        return residuals;
    }
    const ExitHalves exitHalves =
        exitHalvesOf(lifted, exit, lifted.funcType()->exitType());
    if (!exitHalves.ok()) {
        return residuals;
    }

    // Which side of the cut every node is on, as the exit says it: what the forward half computes
    // is computed before the cut, what the backward half computes is computed after it.
    const size_t nodeCount = static_cast<size_t>(lifted.nodeCount());
    std::vector<char> beforeCut(nodeCount, 0);
    {
        std::vector<gc_node_ref_t> stack;
        for (gc_node_ref_t resultHalf : exitHalves.forward) {
            if (resultHalf < nodeCount && beforeCut[resultHalf] == 0) {
                stack.push_back(resultHalf);
                beforeCut[resultHalf] = 1;
            }
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
    // reads -- everything the later half reads is a residual, `with` values included -- so it is a
    // residual, and the backward half reads it out of `lin`'s closure instead of taking a `with`
    // list of its own. Asking it here, and not only through the walk below, is what makes the
    // answer independent of whether the result side happens to read the same weight: a weight only
    // the tangent side reads is still a value that side reads.
    const std::span<const gc_node_ref_t> withPorts = lifted.withPorts();
    const auto isWithPort                          = [&withPorts](gc_node_ref_t ref) {
        return std::find(withPorts.begin(), withPorts.end(), ref) != withPorts.end();
    };
    // The values the body captured where it was written, which are a third kind of port: not a
    // parameter (no place in the pair) and not a `with` port (no argument at the call). A capture
    // the tangent side reads crosses the cut like the other two -- 后一半要读的东西都是残差 -- and
    // the forward half holds the port it enters by (`addCapturePorts`), so it is carried as a
    // residual and read out of `lin`'s closure. Without this arm the walk reached a port with no
    // producer, went nowhere, and the cut refused at it: "the port is not a parameter of the
    // graph".
    const std::span<const gc_node_ref_t> captures = lifted.closureNodes();
    const auto isCaptured                         = [&captures](gc_node_ref_t ref) {
        return std::find(captures.begin(), captures.end(), ref) != captures.end();
    };
    // A value the tangent side reads and the result side already computed crosses the cut: it is a
    // residual, and the walk stops at it (what is inside the forward half is not this function's
    // business). A node the result side never reaches is part of the tangent chain: the walk goes
    // through it.
    std::vector<char> visited(nodeCount, 0);
    std::vector<gc_node_ref_t> found;
    // The calls whose **linear half** crosses the cut, in the order the walk named them: the call
    // site does not see the residuals of the callee's body, it sees
    // `lin_g`.
    std::vector<gc_node_ref_t> linearHalves;
    {
        // Two worklists. `stack` is the walk proper; `later` holds the tangent arguments of such a
        // call, which are computed **after** the cut -- the second half works them out from the
        // tangents it was handed -- even though the result side reaches them (they are inputs of
        // the call it copies). The walk goes through them instead of stopping at them, and it
        // drains the ordinary list first so that a value reached both ways keeps the reading it has
        // today.
        std::vector<gc_node_ref_t> stack;
        std::vector<gc_node_ref_t> later;
        for (gc_node_ref_t tangentRoot : exitHalves.tangent) {
            if (tangentRoot < nodeCount) {
                stack.push_back(tangentRoot);
            }
        }
        while (!stack.empty() || !later.empty()) {
            const bool behindCut    = stack.empty();
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
            if (!behindCut && (isWithPort(ref) || isCaptured(ref))) {
                // A `with` port or a capture has no producer on either side of the cut, so nothing
                // is computed for it and the walk has nowhere further to go: the value is the
                // caller's -- an argument for one, the closure tuple for the other -- and the
                // forward half is the half that holds the port which receives it.
                if (std::find(found.begin(), found.end(), ref) == found.end()) {
                    found.push_back(ref);
                }
                continue;
            }
            // A read of the pair's **tangent** component of a call whose callee answers the pair:
            // what it stands for is that callee's own linear half applied to the tangents the call
            // passes. So the cut carries the half -- the forward half reads it as the second
            // component of what its call answered, the backward half calls it -- and the values
            // inside the callee stay inside it, in its closure.
            if (const gc_node_ref_t call = pairTangentReadOf(lifted, program, ref);
                call != rt::kInvalidNodeRef) {
                const auto *calleeGraph        = calleeGraphOf(lifted, program, call);
                // The linear half's type is the callee's own backward half. A call through a value
                // has no callee graph to ask, so the type comes from the value's signature -- the
                // same halves, read from the only place that exists before the program runs.
                const FunctionType *linearOf =
                    calleeGraph != nullptr && calleeGraph->funcType() != nullptr
                        ? calleeGraph->funcType()
                        : callValueSignatureOf(lifted, call);
                Type *linType = linearOf != nullptr
                                    ? splitHalvesOf(const_cast<FunctionType *>(linearOf)).backward
                                    : nullptr;
                if (linType == nullptr) {
                    // No linear half to name: the call is then only the pair it is, and the branch
                    // below reads it the way it read one before the half existed.
                    stack.push_back(call);
                    continue;
                }
                if (std::find(linearHalves.begin(), linearHalves.end(), call) ==
                    linearHalves.end()) {
                    linearHalves.push_back(call);
                }
                // The call's **tangent arguments** go on the late list: they are what the second
                // half works out from the tangents it was handed, so the cut does not carry them.
                // They are walked
                // -- through the call's own value arguments, which the second half reads and which
                // therefore are residuals -- but with the same rule the result side uses, which is
                // why a call there is descended by its value arguments alone.
                for (gc_node_ref_t argument : tangentArgumentsOf(lifted, program, call)) {
                    if (argument < nodeCount && visited[argument] == 0 &&
                        std::find(later.begin(), later.end(), argument) == later.end()) {
                        later.push_back(argument);
                    }
                }
                continue;
            }
            if (behindCut) {
                // A value the second half computes *after* the cut -- one of the tangent arguments
                // of such a call. The walk goes through it to the values it reads, and never
                // carries the node itself: what it reads crosses the cut by the rules below, one
                // step further in, and its own tangents (a rule kernel's, `callTangentArgsOf`) are
                // the second half's arguments rather than values that cross. Walking into those is
                // what made a parameter's tangent arrive as the parameter's value: the second half
                // then read its own argument out of the closure, and `twice(c + y)` was transposed
                // as `3*(c+y)` instead of `3*(dc+dy)`.
                std::vector<gc_node_ref_t> onward;
                const GraphId lateCalleeId    = lifted.calleeIdOf(ref);
                const auto *lateCallee        = program != nullptr && rt::isValid(lateCalleeId)
                                                    ? program->graph(lateCalleeId)
                                                    : nullptr;
                const std::vector<gc_node_ref_t> arguments = dataInputsOf(&lifted, ref);
                const size_t withArguments                 = lifted.withInputsOf(ref).size();
                const size_t ordinary =
                    arguments.size() > withArguments ? arguments.size() - withArguments : 0;
                if (answersAPair(lateCallee) || answersAPairByValueAt(lifted, ref)) {
                    // Which of the call's ordinary arguments are values is the callee's
                    // signature's answer, read from its type by the one reader of that law
                    // (`valuePortsOf`): the arguments the second half walks into are those ports,
                    // and the call's `with` arguments come after them. This is the same reading
                    // `Half::halfInputsOf` makes at the call it places, so one callee's value
                    // ports are numbered one way in the cut and in the walk over it. A count is
                    // not a reading here: a signature whose second half is not the first half's
                    // tangent spaces is not a lift, and the walk then stops with a name rather
                    // than halving the list.
                    const camel::core::type::FunctionType *lateType =
                        callValueSignatureOf(lifted, ref);
                    if (lateType == nullptr && lateCallee != nullptr) {
                        lateType = lateCallee->funcType();
                    }
                    const std::vector<size_t> valuePorts = valuePortsOf(lateType);
                    if (valuePorts.empty() && ordinary > 0) {
                        if (why != nullptr) {
                            *why = "a call the backward half computes after the cut passes " +
                                   std::to_string(ordinary) +
                                   " ordinary arguments, and its callee's type does not state the "
                                   "seam the tangents stand behind: " +
                                   (lateCallee != nullptr ? lateCallee->name()
                                                          : std::string("a function value"));
                        }
                        return {};
                    }
                    for (size_t port : valuePorts) {
                        if (port < ordinary) {
                            onward.push_back(arguments[port]);
                        }
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
            if (!behindCut && beforeCut[ref] != 0 &&
                std::find(
                    exitHalves.tangent.begin(), exitHalves.tangent.end(), ref) ==
                    exitHalves.tangent.end() &&
                portIndex[ref] == Residual::kNotAParameter) {
                // A call of a callee that answers the pair (value, tangent) reads both sides of the
                // cut and is not itself carried (see the note where the list is filtered below):
                // what the tangent half recomputes the pair from is the values its caller passes,
                // so the walk goes through the call and keeps the value arguments. **Which callees
                // those are is decided by the callee's content** (`answersAPair`, the same
                // criterion the cut itself is built on) -- not by a record of what the callee was
                // lifted from, which would answer differently for a hand-written graph of the same
                // content.
                //
                // A **port** is not one of these, even when the result side reaches it: no half can
                // carry a port as a computed value, and the tangent port in particular is reached
                // by the result side for a reason that has nothing to do with the tangent half -- a
                // call of a pair-answering callee is handed the tangents as ordinary arguments, so
                // the value half walks into the seed of every parameter its callee is called with.
                // Kept here, the seed would go into the closure as "parameter k's value" and the
                // backward half would read the seed's slot where it should read its own port: for
                // `f(x) = 2.0 * x`, `g(x) = f(x) * x`, `g`'s lin came out `mul_tangent(y, x,
                // lin_f(x), x)` -- the value in the tangent's place, a constant function of its
                // argument, 36.0 for every `dx`, and `grad(g)` answered 0.0 without a word.
                // The port branch below is the one that states what a port
                // contributes: the value port of its own parameter, which is the only thing the
                // forward half can hand over.
                const GraphId calleeId         = lifted.calleeIdOf(ref);
                const auto *calleeGraph        = program != nullptr && rt::isValid(calleeId)
                                                     ? program->graph(calleeId)
                                                     : nullptr;
                if (!answersAPair(calleeGraph) && !answersAPairByValueAt(lifted, ref)) {
                    found.push_back(ref);
                    continue;
                }
                const std::vector<gc_node_ref_t> arguments = dataInputsOf(&lifted, ref);
                // Which of those arguments are values is the callee's signature's answer, read
                // from its type (`valuePortsOf`): the ports of `Lift<F>` whose partner is their own
                // tangent space. `dataInputsOf` is the ordinary inputs followed by the context
                // ones, so the ports are numbered in the ordinary list and the call's `with`
                // arguments are added after them. Anything the tangent half reads *beyond* these
                // values is a residual the cut carries, decided by what the callee reads
                // (`residualsOf` above), never by where a parameter stands.
                const size_t withArguments = lifted.withInputsOf(ref).size();
                const size_t valueArguments =
                    arguments.size() > withArguments ? arguments.size() - withArguments : 0;
                const camel::core::type::FunctionType *calleeType =
                    callValueSignatureOf(lifted, ref);
                if (calleeType == nullptr && calleeGraph != nullptr) {
                    calleeType = calleeGraph->funcType();
                }
                for (size_t port : valuePortsOf(calleeType)) {
                    if (port >= valueArguments) {
                        continue;
                    }
                    const gc_node_ref_t input = arguments[port];
                    if (input < nodeCount && visited[input] == 0) {
                        stack.push_back(input);
                    }
                }
                // ...and the context arguments themselves, which are values it reads just as it
                // reads the ordinary ones.
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
                // never reached it -- and what goes into the packet is the parameter's *value*
                // port, not its tangent. The forward half is handed "the value of every parameter
                // in, (result, residuals) out": it has a port for every value and none for a
                // tangent, so a value port is the only thing it can hand over. `xs[i]` is the shape
                // that needs it: the transpose of the read writes its cotangent into
                // `zeros_like(xs)`, a `T[]`'s length is not a thing its type carries, and this
                // parameter holds the array.
                const size_t index = portIndex[ref];
                // Which parameter this port belongs to, asked of the one mapping that knows the
                // layout `jvpSignature` writes (`parameterOfPort`) instead of restating it: a port
                // below the half is its own parameter's value and one above it is the tangent whose
                // value stands exactly `half` places earlier.
                const size_t valuePort = parameterOfPort(lifted.funcType(), index).first;
                // A parameter the tangent side reads crosses too: the residual set is what the
                // backward half actually reads, whether the forward half computed it or it is a
                // parameter itself. The rule does not split by
                // type.
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
    // A call of a callee that answers the pair (value, tangent) is not a value the cut can carry,
    // and the reason is the same one that makes the call readable at all: the pair. The value half
    // does not read tangents, so it answers with the graph the callee's value component comes from
    // (a call of `f`, not of `jvp_of_f`), which is the result and not the pair; the tangent half
    // rebuilds the pair from the values it is handed. A pair built by one half cannot be what the
    // other half reads, so the pair is not a residual -- the values it is made of are (`x` crosses,
    // and the value the call made crosses; the tangent does not). Without this the forward half
    // would hand a float where the tuple says a pair, which is a type error at run time, not a
    // refusal.
    //
    // The callee is named by its **content** (`answersAPair`), the same criterion the cut is built
    // on: a hand-written graph that answers the pair is treated exactly as a graph lifted by `jvp`.
    if (program != nullptr) {
        std::erase_if(found, [&](gc_node_ref_t ref) {
            if (std::find(linearHalves.begin(), linearHalves.end(), ref) != linearHalves.end()) {
                // A call whose linear half crosses is the one call that *is* carried: what crosses
                // is `lin`, which only exists because the forward half made that call.
                return false;
            }
            const GraphId calleeId = lifted.calleeIdOf(ref);
            const rt::GCGraph *calleeGraph =
                rt::isValid(calleeId) ? program->graph(calleeId) : nullptr;
            return answersAPair(calleeGraph) || answersAPairByValueAt(lifted, ref);
        });
    }
    // The linear halves are the remaining members of the list: a computed value like any other, in
    // program order, and after the parameters (they are not parameters).
    for (gc_node_ref_t call : linearHalves) {
        if (std::find(found.begin(), found.end(), call) == found.end()) {
            found.push_back(call);
        }
    }
    // The parameters' values come first, each in its own parameter's slot, so that a closure two
    // programs of one type build cannot mean two different things. The walk can reach one value by
    // two routes -- a
    // parameter's value port seen as a value that crosses the cut, and the same port seen as what a
    // parameter the tangent side reads stands for (`ports[valuePort]` below) -- and while a second
    // copy of a value is not lost information, it must not take a slot that belongs to another
    // parameter. Two programs of one type show why:
    //
    //     sum(x * s)  residuals [x, s, s]     slots 0 and 1 are `x` and `s`
    //     sum(s * x)  residuals [x, x, s]     slot 1 is `x` again, and `s` stands at 2
    //
    // The transposition read slot 1 for `s`, found an array, and its type check refused it --
    // rightly, an array is not the number `s` holds -- then asked for the value it could not find,
    // which added a norm port no call site fills. So a value that appears more than once is placed
    // after everything that appears once: the copies are kept (the count is the graph's answer, and
    // the reading below prints it), and the slots of the parameters stay their own.
    {
        // The value of **every** parameter crosses the cut, not only the ones the tangent half
        // reads (when a transposition needs the zero of an input's shape and no one reads its
        // cotangent, the forward half captures that input, or its shape, as a residual). The
        // transposed half builds that zero with the space's `zeros_like`, which reads its operand's
        // shape, and an open length is nowhere in the type -- the value is the only witness there
        // is. Without it, `transpose` would stand in for the missing witness with the zero the
        // *type* states, an empty array for an open extent, and an unwitnessed zero is wrong at
        // every length but 0.
        //
        // **Added to the parameters' own prefix**, in port order: the ordering block below writes
        // that invariant down (a parameter's value stands in the prefix, a computed value after
        // it), so a parameter the tangent half did not read belongs there too -- appending it at
        // the end would put a parameter after a computed value, and `stop_gradient`'s library path
        // would read the wrong slot and answer `dL/dx=0.0`. The ordering below puts a parameter's
        // value in the prefix, so the values added here are in the prefix in port order and nothing
        // the walk numbered is displaced by more than the count of parameters it did not reach. The
        // transposed half reads them by counting its own parameters (`transpose/walk.cpp`, the zero of
        // an unread parameter -- one reader, and it checks the type before it uses one).
        {
            const size_t half =
                lifted.funcType() != nullptr ? lifted.funcType()->normTypesCount() / 2 : 0;
            // Only a parameter whose **type cannot state its own shape** needs the witness: an
            // array with an extent the declaration does not evaluate (or with none at all). A
            // scalar's zero, and an array whose extents are all written down, are the type's to
            // answer, so the cut carries nothing extra for them -- the reading `residuals=0` still
            // means what it meant for a body whose tangent depends on no input
            // (`stop_gradient_gradient_through_the_library`).
            const auto wantsAWitness = [&](size_t parameter) -> bool {
                Type *const type = lifted.funcType()->normTypeAt(parameter);
                if (type == nullptr || type->code() != camel::core::type::TypeCode::Array) {
                    return false;
                }
                const auto *const arrayType = tt::as_ptr<ArrayType>(const_cast<Type *>(type));
                const std::span<const camel::core::type::ExtentForm> stated =
                    arrayType != nullptr ? arrayType->extents()
                                         : std::span<const camel::core::type::ExtentForm>();
                if (stated.empty()) {
                    return true;
                }
                for (const camel::core::type::ExtentForm &form : stated) {
                    if (!form.evaluate({}).has_value()) {
                        return true;
                    }
                }
                return false;
            };
            for (size_t k = 0; k < half && k < ports.size(); ++k) {
                if (!wantsAWitness(k)) {
                    continue;
                }
                const gc_node_ref_t valuePort = ports[k];
                if (valuePort < nodeCount &&
                    std::find(found.begin(), found.end(), valuePort) == found.end()) {
                    found.push_back(valuePort);
                }
            }
        }
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
            const auto *calleeGraph        = calleeGraphOf(lifted, program, ref);
            const FunctionType *linearOf =
                calleeGraph != nullptr && calleeGraph->funcType() != nullptr
                    ? calleeGraph->funcType()
                    : callValueSignatureOf(lifted, ref);
            residual.parameter  = Residual::kNotAParameter;
            residual.linearHalf = true;
            residual.type       = linearOf != nullptr
                                      ? splitHalvesOf(const_cast<FunctionType *>(linearOf)).backward
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
            residual.type = lifted.node(ref) != nullptr ? lifted.node(ref)->dataType() : nullptr;
        }
        residuals.push_back(residual);
    }

    return residuals;
}

/// The callees whose own cuts a graph's **value half** needs: the pair-answering calls that half
/// copies, found by the same walk it makes -- all of a node's inputs, and at a call to a function
/// that answers the pair the values the caller passes plus the call's `with` arguments, because
/// that is what the forward half of that callee takes. Reserved before either member of this cut is
/// built, so that a body that calls itself names the graph it is being built into.
std::vector<rt::GCGraph *>
pairCalleesOnValueChain(const rt::GCGraph &graph, const rt::Program *program) {
    std::vector<rt::GCGraph *> callees;
    const gc_node_ref_t exit = ungated(&graph, graph.exitNodeRef());
    if (nodeOperatorOf(graph, exit) != NodeOperator::Fill || graph.funcType() == nullptr) {
        return callees;
    }
    const ExitHalves answerHalves =
        exitHalvesOf(graph, exit, graph.funcType()->exitType());
    if (!answerHalves.ok()) {
        return callees;
    }
    const size_t nodeCount = static_cast<size_t>(graph.nodeCount());
    std::vector<char> visited(nodeCount, 0);
    std::vector<gc_node_ref_t> stack;
    for (gc_node_ref_t resultHalf : answerHalves.forward) {
        if (resultHalf >= nodeCount || visited[resultHalf] != 0) {
            continue;
        }
        visited[resultHalf] = 1;
        stack.push_back(resultHalf);
    }
    while (!stack.empty()) {
        const gc_node_ref_t ref = stack.back();
        stack.pop_back();
        std::vector<gc_node_ref_t> inputs;
        if (answersAPairAt(graph, program, ref)) {
            const GraphId calleeId = graph.calleeIdOf(ref);
            if (program != nullptr && rt::isValid(calleeId) &&
                program->graph(calleeId) != nullptr) {
                callees.push_back(program->graph(calleeId));
            }
            const std::vector<gc_node_ref_t> all = dataInputsOf(&graph, ref);
            // The values the caller passes, read from the callee's signature (`valuePortsOf`), and
            // then the call's `with` arguments, which that reading does not name.
            const rt::GCGraph *callee =
                program != nullptr && rt::isValid(calleeId) ? program->graph(calleeId) : nullptr;
            const camel::core::type::FunctionType *calleeType = callValueSignatureOf(graph, ref);
            if (calleeType == nullptr && callee != nullptr) {
                calleeType = callee->funcType();
            }
            const size_t withCount = graph.withInputsOf(ref).size();
            const size_t ordinary  = all.size() > withCount ? all.size() - withCount : 0;
            for (size_t port : valuePortsOf(calleeType)) {
                if (port < ordinary) {
                    inputs.push_back(all[port]);
                }
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
    // is not refused with "a captured value has no port to enter them by": the closure has
    // capture ports of its own -- and a `jvp` that lifts a
    // weight, which is what design principles 9.9's `vjp = transpose(split(jvp(f)))` needs, can
    // only be cut if `split` takes such a function.
    const size_t count = function->normTypesCount();
    if (count == 0 || count % 2 != 0) {
        halves.why = "`split` takes a lifted function, whose parameters are the value of every "
                     "parameter and then the tangent of every one, so they come in two halves of "
                     "equal length";
        return halves;
    }
    if (!function->hasExitType()) {
        halves.why = "`split` takes a lifted function, whose result is the pair (result, tangent)";
        return halves;
    }
    // Where the cut is made is `Lift<exit>`'s shape (`liftedShapeOf`, lift.h), and it has two
    // forms:
    //
    //   - `Lift<R>` is the pair `(R, Tangent<R>)` -- the ordinary case, cut along the two members;
    //   - `Lift<R>` is a tuple of member lifts -- a result that holds a function. It is cut **by
    //     component**, which is what `split.h`'s own sentence says the cut is ("along the pair, not
    //     along a position"): a member with a tangent space gives its value to the forward half and
    //     its tangent to the backward half, a function member gives the lifted function to the
    //     forward half and nothing to the backward half (a function has no tangent). `LiftedMember`
    //     reads each member back (`lift.h`), so this asks the law and never guesses at a two-tuple.
    //
    // Both forms end in the same two types -- the forward half's answer value `result` and the
    // backward half's answer `tangent` -- so everything below this point is one code path.
    const LiftedShape liftedExit = liftedShapeOf(function->exitType());
    Type *result                 = nullptr;
    Type *tangent                = nullptr;
    if (liftedExit.pair) {
        if (function->exitType()->code() != TypeCode::Tuple) {
            halves.why =
                "`split` takes a lifted function, whose result is the pair (result, tangent)";
            return halves;
        }
        const auto *exitPair = tt::as_ptr<TupleType>(function->exitType());
        if (exitPair->size() != 2) {
            halves.why = "`split` takes a lifted function, whose result is the pair (result, "
                         "tangent); this one answers " +
                         std::to_string(exitPair->size()) + " values";
            return halves;
        }
        result  = exitPair->typeAt(0);
        tangent = exitPair->typeAt(1);
        if (result == nullptr || tangent == nullptr) {
            halves.why = "the pair a lifted function answers with has a component without a type";
            return halves;
        }
    } else if (liftedExit.type != nullptr) {
        const auto *lifted = tt::as_ptr<TupleType>(function->exitType());
        std::vector<Type *> forwardMembers;
        std::vector<Type *> tangentMembers;
        forwardMembers.reserve(lifted->size());
        tangentMembers.reserve(lifted->size());
        for (size_t index = 0; index < lifted->size(); ++index) {
            const LiftedMember member = liftedMemberOf(const_cast<Type *>(lifted->typeAt(index)));
            if (member.forward == nullptr) {
                halves.why = "`split` takes a lifted function whose result lifts by component, and "
                             "member " +
                             std::to_string(index) +
                             " of that result is not a form `Lift` writes: "
                             "it is neither a `(value, tangent)` pair nor a lifted function";
                return halves;
            }
            forwardMembers.push_back(member.forward);
            if (member.value) {
                tangentMembers.push_back(member.tangent);
            }
        }
        // The forward half answers one member per component; the backward half takes the cotangent
        // of what the forward half answers, which is one member per **value** component -- a
        // function component takes no cotangent, the same rule `transpose` applies to a closure
        // member.
        result = forwardMembers.size() == 1 ? forwardMembers.front()
                                            : TupleType::create(std::move(forwardMembers));
        if (tangentMembers.empty()) {
            tangent = TupleType::create(std::vector<Type *>{});
        } else {
            tangent = tangentMembers.size() == 1 ? tangentMembers.front()
                                                 : TupleType::create(std::move(tangentMembers));
        }
        if (result == nullptr || tangent == nullptr) {
            halves.why =
                "the component-wise answer of a lifted function has a member without a type";
            return halves;
        }
    } else {
        halves.why = "`split` takes a lifted function, whose result lifts member by member; this "
                     "one's result is a `" +
                     function->exitType()->toString() + "`, which `Lift` does not answer for";
        return halves;
    }

    // The residuals: the values the backward half reads, in program order. Which values those are
    // is a property of the *graph*, so the graph answers it (`residualsOf`), and the tuple it names
    // is `lin`'s
    // **closure** -- the values travel with the function value, not in a parameter of it (the
    // residuals are the backward half's capture ports). A caller with no graph (the operator
    // resolver, which sees types only) names none, and then the closure is empty: a signature has
    // no reading to name, and the count a reading states comes from the graph
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
    // The two halves are the two halves of the list: the values are ports `0 .. n`, the tangents
    // are `n .. 2n` (`jvpSignature`). The forward half takes the values in the
    // parameters' order and the backward half the tangent that stands in the same place -- which is
    // what makes a `()` slot for a parameter with no tangent space mean something rather than
    // swallow its neighbour's tangent.
    const size_t half = count / 2;
    for (size_t i = 0; i < half; ++i) {
        forwardParams.emplace_back(function->normTypeAt(i), false);
        // A parameter with no tangent space stands in the lifted list as `()`, and `()` is an
        // **answer** the backward half gives, not a parameter it takes: `Vjp<F>` is
        // `(values...) => (result, (ct) => (tangents...))`, so only a tangent that is something can
        // be passed in, while the answer has one slot per parameter. Getting this wrong one way
        // makes the half take an argument nobody passes; the other way makes it answer fewer than
        // it declares.
        Type *tangentOfParameter = function->normTypeAt(i + half);
        if (tangentOfParameter != nullptr &&
            camel::core::tangentSpaceOf(tangentOfParameter).hasSpace()) {
            tangentParams.emplace_back(tangentOfParameter, false);
        } else {
            // The parameter's tangent is `()`: an **answer** the half gives, not an argument it
            // takes. It keeps its place in the half's type so that the transposed half answers one
            // slot per parameter -- `Vjp<F>`'s pullback is `(ct) => (..Tangent<ParamsOf<F>>)`, and
            // `Tangent<int>` is `()` -- while the half's *graph* declares no port for it, because
            // nobody can pass a value where there is nothing to pass (the port loop below skips it
            // by the same predicate). Losing the place here makes the pullback answer one member
            // fewer than it declares: `grad_array_with_int.fvm` read `(float[],)` where the
            // declaration said `(float[], ())`.
            tangentParams.emplace_back(TupleType::create(std::vector<Type *>{}), false);
        }
    }

    // The lift's `with` parameters, on the **forward** half only: the pullback of a function with
    // `with` ports takes no `with` list, it captures them in its closure. The forward half reads a
    // weight to compute the value, and a weight is a value the call supplies, so its signature
    // declares the list and `addWithPorts` gives it one port per member. The backward half does
    // not: what it reads after the cut is a residual by the one rule --
    // 后一半要读的东西都是残差，进 `lin` 的闭包 -- and a `with` value is one of them, so `lin` is a
    // function of the tangents alone and `transpose(lin)` is the declared `Pullback<F>` `(ct) =>
    // (..Tangent<ParamsOf<F>>)` with no list of its own. The list is still read off the lift's
    // type, so a caller writes the `with` values once, where `vjp`'s lambda takes them.
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
    // `with` value among them (`residualsOf`) -- so its type holds no residual position and no
    // `with` list: at a call site `lin` is an ordinary function type, so there is no arity a
    // signature could not know, and no guard has to be loosened to admit one.
    halves.backward = FunctionType::create(param_vec_t(), tangentParams, tangent)
                          ->withResources(function->resources());
    // The forward half answers `(result, lin)`: the value and the linear function closed over the
    // residuals it was computed with. `lin`'s type is the backward half's, so the pair's second
    // member is spelled `(tangents...) => tangent` and the closure stays where it belongs -- off
    // the type.
    TupleType *forwardExit = TupleType::create(std::vector<Type *>{result, halves.backward});
    halves.forward         = FunctionType::create(withParams, forwardParams, forwardExit)
                                 ->withResources(function->resources());
    halves.closure         = closure;
    // What the call `split(f)` answers: the forward half, which hands out `(y, lin)`. There is no
    // second component to hand out: `lin` does not exist until the residuals do, so the forward
    // half is the one that makes it.
    halves.pair = halves.forward;
    return halves;
}

/// One arm of a branch, as the cut needs it: the region it lives in, its number in the branch, and
/// the two nodes that hold its halves of the pair. `value` is what the forward half answers with,
/// `tangent` what the backward half answers with -- both read off the arm's yield, never guessed.
struct ArmCut {
    uint32_t region       = 0;
    uint32_t index        = 0;
    gc_node_ref_t value   = rt::kInvalidNodeRef;
    gc_node_ref_t tangent = rt::kInvalidNodeRef;
};

/// Builds one half of a branch cut: the half's own Brch and Join, one region per arm, and the arm's
/// half of the pair copied into it. `tangentSide` says which half of the pair this half keeps, and
/// `conditionCopy` is the condition as this half reads it -- the forward half computes it, the
/// backward half takes it out of the residual tuple, which is what carrying it across the cut
/// means.
///
/// The shape is the one `jvp` builds when it lifts a branch (`modules/transforms/jvp.cpp`): the
/// Join is made first and the Brch names it, then the Join is rewritten to take the Brch as its
/// norm input and the arms' results as its with inputs, one region per arm carrying the result it
/// yields. What is different here is only where an arm's body comes from: the source graph's
/// region, copied by the same `Half` walk that copies a straight-line body.
///
/// The draft is a template parameter on purpose: this file builds through what the transaction
/// hands it and never names the type, which is the direct connection `runtime.graph_draft_ledger`
/// counts and may only see fall. A parameter written as the type is one more of those references
/// (+1, and the ledger says so by file and name); taking it as a parameter the caller's transaction
/// already fixed keeps the count where it was.
template <typename Draft>
/// One arm of a branch, cut on its own region: the arm's region is made in the half's draft, the
/// half's walk copies the arm's body *into that region* -- the same `copyHalf` the straight-line
/// body uses -- and the value the arm hands the Join comes back. The unit here is a **region**, not
/// an arm-shaped copy of the whole branch: that is what lets the arm of a
/// nested branch enter this function too, one region at a time, instead of an arm's body being
/// copied whole.
bool cutArm(
    Draft &draft, Half &half, const ArmCut &arm, bool tangentSide, gc_node_ref_t brch,
    SplitGraph &refusal, gc_node_ref_t &armResult) {
    const gc_node_ref_t root = tangentSide ? arm.tangent : arm.value;
    if (root == rt::kInvalidNodeRef) {
        return refuseAt(
            refusal,
            half.source,
            rt::kInvalidNodeRef,
            "an arm",
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
            refusal,
            half.source,
            rt::kInvalidNodeRef,
            "the branch",
            "the branch has no arm or no condition the cut can name");
    }
    const gc_node_ref_t join = draft.addJoinNode(joinType, rt::kInvalidNodeRef, armCount);
    const gc_node_ref_t brch = draft.addBrchNode(Type::Int64(), join, armCount);
    // The condition is not an arm's input: it is a value of the region the branch stands in, so
    // each half reads it there and both halves read the *same* value -- that is what makes the pick
    // agree.
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
    // The Join names the Brch and takes the arms' results, written the way `jvp` writes it so the
    // two writers of a branch in this tree state a branch the same way.
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
    init.payload = std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(&joinBody),
        sizeof(joinBody));
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
    // What crosses the cut: the value of every value parameter the arms read, and the condition.
    // The condition is a residual because the backward half must pick the arm by the *same* value
    // the forward half picked it by (design 4.2), not by computing the comparison again.
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
        one.type =
            function->node(condition) != nullptr ? function->node(condition)->dataType() : nullptr;
        cuts.push_back(one);
    }
    std::vector<Type *> cutTypes;
    cutTypes.reserve(cuts.size());
    for (const Residual &one : cuts) {
        if (one.type == nullptr) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Residual)
               << " (a value the cut carries has no type)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Residual));
            return built;
        }
        cutTypes.push_back(one.type);
    }
    Type *cutTuple                 = TupleType::create(cutTypes);
    const SplitHalves branchHalves = splitHalvesWith(function->funcType(), cutTuple);
    if (branchHalves.forward == nullptr || branchHalves.backward == nullptr) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Signature) << " (" << branchHalves.why
           << ")\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Signature));
        return built;
    }
    Type *valueType   = function->node(arms.front().value) != nullptr
                            ? function->node(arms.front().value)->dataType()
                            : nullptr;
    Type *tangentType = function->node(arms.front().tangent) != nullptr
                            ? function->node(arms.front().tangent)->dataType()
                            : nullptr;
    if (valueType == nullptr || tangentType == nullptr) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Arm)
           << " (an arm's half of the pair has no type)\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Arm));
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
        // Everything that crosses the cut is made *before* the branch is built, in the region the
        // branch
        // stands in. A residual the walk projects while an arm is open would be created inside that
        // arm's region and then reused by the other arm -- a node of two arms, which the branch-arm
        // invariant refuses (`it would run only when ...`). So the residuals are projected here, in
        // region 0.
        for (const Residual &one : cuts) {
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
        gc_node_ref_t join = rt::kInvalidNodeRef;
        if (!buildHalfBranch(
                draft,
                forward,
                forward.copyOf[condition],
                arms,
                false,
                valueType,
                built,
                join)) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Forward)
               << " construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                            .atConstruct(built.refusedConstruct));
            return built;
        }
        std::vector<gc_node_ref_t> residualMembers;
        residualMembers.reserve(cuts.size());
        for (const Residual &one : cuts) {
            gc_node_ref_t value = forward.copyOf[one.node];
            if (value == rt::kInvalidNodeRef && one.parameter != Residual::kNotAParameter) {
                // A value parameter the arms did not happen to read still crosses: the half's port
                // for it is the value itself, which is what a residual is for this kind of member.
                const size_t index = one.parameter;
                if (index < forward.portOf.size()) {
                    value = forward.portOf[index];
                }
            }
            if (value == rt::kInvalidNodeRef) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Residual)
                   << " (the forward half does not compute a value the cut carries)\n";
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Residual));
                return built;
            }
            residualMembers.push_back(value);
        }
        // `lin` for a branch body: the same closure the straight-line tier builds, closed over the
        // values the cut carries -- the condition among them (design 4.2), which is what lets the
        // backward half pick the arm by the value the forward half picked it by.
        const gc_node_ref_t linValue = buildLinearClosure(
            draft,
            program,
            backwardId,
            branchHalves.closure,
            branchHalves.backward,
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> exitHalves{join, linValue};
        const gc_node_ref_t out = fillTuple(
            draft,
            branchHalves.forward->exitType(),
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
            Type *type = function->funcType()->normTypeAt(i);
            if (type == nullptr || !camel::core::tangentSpaceOf(type).hasSpace()) {
                // A () slot is an answer this half gives, never an argument it takes.
                continue;
            }
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            backward.portOf[i] = port;
        }
        // The residuals, projected in region 0 for the same reason as the forward half: an accessor
        // made inside an arm would belong to that arm and be reused by the other one.
        for (const Residual &one : cuts) {
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
        gc_node_ref_t join = rt::kInvalidNodeRef;
        if (!buildHalfBranch(
                draft,
                backward,
                backward.copyOf[condition],
                arms,
                true,
                tangentType,
                built,
                join)) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Backward)
               << " construct=" << built.refusedConstruct
               << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
               << " why=" << built.refusedWhy << '\n';
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward)
                            .atConstruct(built.refusedConstruct));
            return built;
        }
        draft.setExitNode(join);
        draft.setOutputNode(join);
        draft.setReturnNode(join, rt::GCReturnKind::Self);
    }
    group.encode();
    const auto *forwardMade         = group.encoded(forwardId);
    const auto *backwardMade        = group.encoded(backwardId);
    built.residuals                 = cuts.size();
    os << "REINTERP split function=" << function->name() << " forward=" << rt::toValue(forwardId)
       << " backward=" << rt::toValue(backwardId) << " type="
       << (forwardMade != nullptr && forwardMade->funcType() != nullptr
               ? forwardMade->funcType()->toString()
               : std::string("-"))
       << " residuals=" << built.residuals << " arms=" << arms.size();
    if (function->recomputesOnBackward()) {
        os << " recompute=1";
    }
    os << '\n';
    built.program  = rt::ProgramHold(*context, group.program());
    built.forward  = const_cast<rt::GCGraph *>(forwardMade);
    built.backward = const_cast<rt::GCGraph *>(backwardMade);
    return built;
}
/// Which shape a lifted graph's exit has. Asked once, so that the cut has one entry and four cells
/// instead of a chain of tests: the exit is the pair itself (a straight-line body), a Join a branch
/// names (the condition picks an arm), a Join no branch names (a loop), or something that is not
/// the pair at all. The three that can be cut are cells of the same entry because the cut that will
/// handle them is one traversal over regions and a shape that is refused
/// is a cell too, so that its refusal stays named instead of becoming a fall-through.
enum class ExitShape { Straight, Branch, Loop, NotAPair };

/// One half of a lifted exit, read back to the LOOP cell it comes from: the loop, and the nodes
/// that read each of its two components (0 the value, 1 the tangent), which are the projections a
/// half seeds with its own value for the component, so the chain above the loop is copied without
/// the walk entering the cell. `ok` is false when the half reads no loop, or reads two, or reaches
/// one other than through a `TupleIndex` of it.
///
/// The two components are kept apart rather than folded into one "which component" answer: a chain
/// may read *both*, and that is not the same fact as reading one. `jvp_idx_arr` states the length
/// of the array it reads (`pick_at(dxs, i, len(xs))`) so that the transposed rule never reads a
/// linear operand, and the tangent side of an escape therefore reads `len(xs)` off the loop's
/// **value** while the tangent itself comes from the loop's **tangent** (as in
/// `vjp_loop.rounds_are_undone_in_reverse`: the tangent root is `pick_at(35, 0, len(34))`, 34
/// reading component 0 and 35 component 1). One answer for "which component" cannot
/// hold that, and refusing it made the exit unreadable.
struct LoopReach {
    bool ok            = false;
    gc_node_ref_t loop = rt::kInvalidNodeRef;
    /// The `TupleIndex` nodes that read component 0 (the value) and component 1 (the tangent).
    std::vector<gc_node_ref_t> valueProjections;
    std::vector<gc_node_ref_t> tangentProjections;
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
        visited[ref]           = 1;
        const rt::GCNode *node = function->node(ref);
        if (node->kind == rt::GCNodeKind::Loop) {
            if (reach.loop != rt::kInvalidNodeRef && reach.loop != ref) {
                return LoopReach{}; // two loops: the chain is not a projection of one cell
            }
            reach.loop = ref;
            continue;
        }
        // A projection of the loop's pair is the edge into the cell, and its component is what the
        // half reads. Any other node reading the loop itself would hold the cell's pair as one
        // value, which no half can be handed (`split` hands the components, not the pair).
        const std::vector<gc_node_ref_t> inputs = dataInputsOf(function, ref);
        bool readsTheCell                       = false;
        for (gc_node_ref_t input : inputs) {
            const rt::GCNode *source =
                input < function->nodeCount() ? function->node(input) : nullptr;
            readsTheCell =
                readsTheCell || (source != nullptr && source->kind == rt::GCNodeKind::Loop);
        }
        if (readsTheCell) {
            const std::optional<MemberRead> read = memberReadOf(*function, ref);
            if (!read.has_value() || read->kind != rt::GCAccsKind::TupleIndex || read->place > 1) {
                return LoopReach{};
            }
            // Which component, kept as which component. A chain that reads both is not refused
            // here: the half that reads the *other* component is reading a value computed from the
            // loop's result, which crosses the cut as a residual taken after the loop
            // (`afterLoop`).
            std::vector<gc_node_ref_t> &into =
                read->place == 0 ? reach.valueProjections : reach.tangentProjections;
            if (std::find(into.begin(), into.end(), ref) == into.end()) {
                into.push_back(ref);
            }
        }
        for (gc_node_ref_t input : inputs) {
            if (input < function->nodeCount() && visited[input] == 0) {
                stack.push_back(input);
            }
        }
    }
    reach.ok = reach.loop != rt::kInvalidNodeRef &&
               (!reach.valueProjections.empty() || !reach.tangentProjections.empty());
    return reach;
}

ExitShape exitShapeOf(const rt::GCGraph *function, gc_node_ref_t exit, LoopCut &cut) {
    const rt::GCNode *node = function->node(exit);
    if (node == nullptr) {
        return ExitShape::NotAPair;
    }
    if (nodeOperatorOf(*function, exit) == NodeOperator::Fill) {
        const auto fill = fillPayloadOf(*function, exit);
        if (fill && fill->fillKind == rt::GCFillKind::Tuple && fill->size() == 2) {
            // The pair may be the loop cell itself pair of components, read through a chain above
            // the cell: `jvp` gives a loop the pair type, and a program that reads *into* the
            // loop's value
            // (`g(s) = (loop x = s ...)[0]`) answers the pair by projecting one component on each
            // side. The cut then enters the loop through its cell all the same -- the cell is one
            // node inside the chain the exit reads, not the exit -- and the two chains are what
            // each half computes after the loop. For example
            // (`vjp_loop.rounds_are_undone_in_reverse`): exit 37 = fill[36, 30, 31], 30 =
            // idx_arr(26, 29), 26 = tuple-index 0 of loop 25.
            const std::span<const gc_node_ref_t> halves = function->withInputsOf(exit);
            const LoopReach value                       = loopBehindExit(function, halves[0]);
            const LoopReach tangent                     = loopBehindExit(function, halves[1]);
            // What each chain must answer: the value chain the cell's value and nothing of its
            // tangent, the tangent chain the cell's tangent. A value-side read *on the tangent
            // chain* is allowed and is not the tangent half answering the value: it is a value
            // computed from the loop's result (the length `jvp_idx_arr` states), which crosses the
            // cut as a residual taken after the loop. The converse is not: a value
            // that read the tangent would need a tangent the forward half does not have.
            const bool valueIsTheValue = value.ok && value.loop == tangent.loop &&
                                         value.tangentProjections.empty() &&
                                         !value.valueProjections.empty();
            const bool tangentIsTheTangent =
                tangent.ok && tangent.loop == value.loop && !tangent.tangentProjections.empty();
            if (valueIsTheValue && tangentIsTheTangent) {
                cut.loop            = value.loop;
                cut.exitValueRoot   = halves[0];
                cut.exitTangentRoot = halves[1];
                // Every component-0 projection either chain reads: the forward half seeds them all
                // with the cell's value, because a residual taken after the loop may read one of
                // them.
                cut.valueProjections = value.valueProjections;
                for (gc_node_ref_t projection : tangent.valueProjections) {
                    if (std::find(
                            cut.valueProjections.begin(),
                            cut.valueProjections.end(),
                            projection) == cut.valueProjections.end()) {
                        cut.valueProjections.push_back(projection);
                    }
                }
                std::sort(cut.valueProjections.begin(), cut.valueProjections.end());
                // Only the component-1 projections: the backward half seeds these with its own
                // tangent state, and a component-0 one seeded there would carry the wrong value.
                cut.tangentProjections = tangent.tangentProjections;
                return ExitShape::Loop;
            }
            return ExitShape::Straight;
        }
    }
    if (node->kind == rt::GCNodeKind::Loop) {
        // A loop answers with its own value: `jvp` gives the LOOP node the pair type, so the exit
        // of `jvp(loop body)` is the loop itself and its two components *are* the pair. There is a
        // pair to cut in that graph all the same -- it is the node's value, not a tuple fill beside
        // it.
        if (function->funcType() != nullptr &&
            node->dataType() == function->funcType()->exitType()) {
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
    // the pair, or the Join a branch answers with. `exitShapeOf` is the one reader of that
    // question, so this asks it rather than restating its conditions.
    LoopCut cut;
    const gc_node_ref_t exit = ungated(callee, callee->exitNodeRef());
    if (exitShapeOf(callee, exit, cut) == ExitShape::NotAPair) {
        return fail(
            "its exit is not the pair (value, tangent): it is neither a two-member tuple fill, "
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
/// Only the forward half declares it. The backward half reads a `with`
/// value it needs out of `lin`'s closure, where `residualsOf` put it as a residual, so it has no
/// port of its own to give it and no list to state; `lin` being an ordinary function of the
/// tangents is what keeps the pullback's type free of the `with` list.
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

/// The values the source **captured** where it was written, as closure ports of one half. A capture
/// is a port of the source graph (`GCGraph::closureNodes`) that is neither a parameter nor a `with`
/// parameter: the lifted body reads it, and a half that declared no port for it would refuse that
/// read with "the port is not a parameter of the graph" -- which is what the tensor-`with` shape
/// did before this function handled captures (for example `vjp((ww) => dot<ww>(x))(w)`, where `x`
/// is a capture of the lambda and `w` is `dot`'s `with` parameter; the cut of `jvp_of_dot` was fine
/// and the cut of the lambda was refused).
///
/// The half declares them in the source's order and takes the source's closure type, which is the
/// layout the operator hands over: `splitKernel` creates the forward function value with `f`'s own
/// tuple and copies every slot into it, so the ports this declares read the caller's values and the
/// call site fills them the way it always filled a closure. That is the shape for a
/// closure port (forwarded into the member's own port, filled by the call site as usual), and it is
/// the same thing the `with` list does one road up -- the difference is only where the value
/// travels: a `with` value is an argument of the call, a capture is a member of the function value.
///
/// Only the forward half declares them, for the reason `addWithPorts` gives: what the backward half
/// reads is a residual, and a capture is one (`residualsOf`), so it arrives in `lin`'s closure.
/// Declaring every capture, and not only the ones some walk reached, is what keeps this side's
/// closure type the source's: a half that declared a subset would index a tuple it does not hold.
bool addCapturePorts(Half &half, const rt::GCGraph *function) {
    if (function == nullptr) {
        return true;
    }
    const std::span<const gc_node_ref_t> captures = function->closureNodes();
    if (captures.empty()) {
        return true;
    }
    // The type the source states for its closure, when it states one. A capture's type is on the
    // port node either way, and a graph whose closure layout is only in the function value that
    // carries it (`GCGraph::closureType()` is null on it -- a lambda written where it is used is
    // one) states nothing here: the operator supplies that tuple type when it hands the half out
    // (`splitKernel`, `function->tupleType()`), so declaring the ports is the whole of what a half
    // with no stated layout has to do. A stated layout that does not hold the captures is not
    // copied: a type that names fewer members than the ports is worse than no type at all.
    const auto *closureType = function->closureType();
    if (closureType != nullptr && closureType->size() != captures.size()) {
        return false;
    }
    for (gc_node_ref_t original : captures) {
        const rt::GCNode *node = function->node(original);
        Type *captureType      = node != nullptr ? node->dataType() : nullptr;
        if (captureType == nullptr) {
            return false;
        }
        const gc_node_ref_t port =
            half.draft.addPortNode(captureType, half.draft.allocateRuntimeSlot(captureType));
        half.draft.appendClosureNode(port);
        half.capturePortOf[original] = port;
    }
    if (closureType != nullptr) {
        half.draft.setClosureType(const_cast<camel::core::type::TupleType *>(closureType));
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
    // asks a signature how many residuals there are -- a signature is computed from types, and
    // types cannot tell `f(x) = x` (no residuals) from `f(x) = x*x` (one), which have the same
    // lifted type.
    std::string residualWhy;
    const std::vector<Residual> graphResiduals = residualsOf(*function, program, &residualWhy);
    if (!residualWhy.empty()) {
        // The walk could not answer, and said why: a call after the cut whose callee's type does
        // not state the seam. The cut stops here by name rather than reading an empty list as "no
        // residuals" -- the two are different answers and only one of them has a graph behind it.
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Tangent) << " (" << residualWhy << ")\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Tangent));
        return SplitGraph{};
    }
    std::vector<Type *> residualTypes;
    residualTypes.reserve(graphResiduals.size());
    for (const Residual &residual : graphResiduals) {
        if (residual.type == nullptr) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Residual) << " ("
               << "a value the backward half reads has no type" << ")\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Residual));
            return SplitGraph{};
        }
        residualTypes.push_back(residual.type);
    }
    // **The closure is the residuals, and nothing else.** A parameter's own value the backward half
    // reads is one of them -- `residualsOf` numbers it, and the member carries the parameter number
    // it is the value of. There is no tail of parameter types after the residuals: what a member
    // is the value of is its identity, and a reader that had to count places after the residuals
    // was counting a layout two sides had to agree on.
    Type *residualsType      = TupleType::create(residualTypes);
    const SplitHalves halves = splitHalvesWith(function->funcType(), residualsType);
    if (halves.forward == nullptr || halves.backward == nullptr) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Signature) << " (" << halves.why << ")\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Signature));
        return SplitGraph{};
    }
    // The exit of a lifted function is the pair (result, tangent): the first component is what the
    // forward half computes, the second is where the tangent comes from.
    const gc_node_ref_t exit = ungated(function, function->exitNodeRef());
    if (exit >= function->nodeCount() || function->node(exit) == nullptr) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Exit) << "\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Exit));
        return SplitGraph{};
    }
    const rt::GCNode *exitNode = function->node(exit);
    (void)exitNode;
    // What the exit's shape says about where the loop cell is: filled by the shape question, read
    // by the loop cell of the switch below (the pair's two chains above one loop) and, before that,
    // by the dispatch the loop takes with its own two halves.
    LoopCut loopCut;
    // The loop is the one cell that does not read a residual tuple out of the graph before the cut:
    // its residuals are the tape the forward half makes plus the invariants its rounds read, and
    // neither exists until the loop is read. So it dispatches here, before `residualsOf`, and takes
    // its own two halves with it -- the entry is the same question about the same exit's shape.
    if (exitShapeOf(function, exit, loopCut) == ExitShape::Loop) {
        return cutLoopGraph(context, program, function, os, loopCut);
    }
    // One entry, three cells. Instead of a chain of `if`s -- "is it a tuple fill? then straight
    // line; else is it a Join? then branch; else loop; else not a pair" -- the *shape* of the exit
    // is one question, asked once, answered by one `switch` over the four answers. The shapes it
    // separates, and why they are cells of one entry rather than three roads:
    //
    // - The pair does not have to be a tuple fill. A branch answers with its Join, and `jvp` gives
    //   that Join the lifted type, so the exit of `jvp(branch body)` is a Join whose slots hold the
    //   pair: one arm writes (value, tangent) into it and the other writes its own. There is a pair
    //   to cut in that graph all the same -- the rule for it is design 4.2: the condition
    //   itself is a residual, and the backward half picks the same arm by evaluating that
    //   condition. What `split` cannot do yet is step *inside* an arm: an arm is a region, not a
    //   data input, so `Half`'s walk (which follows `dataInputsOf`) does not reach it. Saying that
    //   is the point of this reading -- a `skip=exit` that does not say which exit leaves the
    //   reader with nothing.
    // - A loop is its own shape: the exit of `jvp(loop body)` is the LOOP node, whose two value
    //   components are the pair, and its residuals are the tape the cut makes -- so it is
    //   dispatched above, before `residualsOf` reads a residual tuple the graph does not have
    //   (cutLoopGraph).
    // - Every cell *starts* at the same place (the exit's shape) and will end at the same place (a
    //   cut that recurses by region); they are written as cells so that a recursion by region
    //   is a change of one cell's body, not the invention of a second entry.
    switch (exitShapeOf(function, exit, loopCut)) {
    case ExitShape::Straight:
        // The exit is the pair itself: the cut is the straight-line body, and the two `copyHalf`
        // walks after this switch are that cell's one entry.
        break;
    case ExitShape::Branch: {
        const gc_node_ref_t condition = branchConditionOf(function, exit);
        SplitGraph refusal;
        {
            // The first step of the cut a branch needs: enter each arm and say what is in it. The
            // walk a half does follows `dataInputsOf` and never reaches an arm, so this is the
            // entrance it does not have yet -- and the numbers are what the next step builds on (an
            // arm is entered through its form ports, and it is not empty).
            const gc_node_ref_t branch = branchNodeOf(function, exit);
            for (uint32_t region = 1; region < function->regionCount(); ++region) {
                const rt::GCRegion *one = function->region(region);
                if (one == nullptr || one->kind != rt::GCRegionKind::Arm || one->owner != branch) {
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
            // No probe draft is made here: how many members an arm has and which values they
            // read from outside it are numbers the source graph answers directly, and `from=[...]`
            // names source nodes.
            Type *pairType = exitNode->dataType();
            // The condition that selects among the arms. A ref that is not a node of this
            // graph is in no region -- 0 stands for "the body, and so in none of the arms
            // this loop walks" -- and the region query is not asked about it, because it
            // would not answer the body for a node that is not there.
            const uint32_t conditionRegion =
                function->containsNodeRef(condition) ? function->nodeRegion(condition) : 0;
            for (uint32_t region = 1; region < function->regionCount(); ++region) {
                const rt::GCRegion *one = function->region(region);
                if (one == nullptr || one->kind != rt::GCRegionKind::Arm || one->owner != branch) {
                    continue;
                }
                gc_node_ref_t yield = rt::kInvalidNodeRef;
                for (gc_node_ref_t r = 0; r < function->nodeCount(); ++r) {
                    const rt::GCNode *n = function->node(r);
                    if (n == nullptr || function->nodeRegion(r) != region ||
                        nodeOperatorOf(*function, r) != NodeOperator::Fill ||
                        n->dataType() != pairType) {
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
                   << " yield=" << yield << " members=" << members.size()
                   << " value=" << (members.size() == 2 ? members[0] : rt::kInvalidNodeRef)
                   << " tangent=" << tangent << " inputs=" << fromOutside.size() << " from=[";
                for (gc_node_ref_t ref : fromOutside) {
                    // Named by what it is in the source graph, not by the probe's ref: a
                    // parameter is `p<index>` (the two the lifted graph takes are the value
                    // and the seed), anything else `n<ref>`.
                    const rt::GCNode *read                     = function->node(ref);
                    const std::span<const gc_node_ref_t> ports = function->normPorts();
                    const auto found = std::find(ports.begin(), ports.end(), ref);
                    if (read != nullptr && read->kind == rt::GCNodeKind::Port &&
                        found != ports.end()) {
                        os << 'p' << std::distance(ports.begin(), found) << ' ';
                    } else {
                        os << 'n' << ref << ' ';
                    }
                }
                os << "] condition=" << condition << " conditionRegion=" << conditionRegion << '\n';
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
                refusal,
                function,
                exit,
                "the exit",
                "this graph's exit is the Join of a branch (the arm the condition picks writes the "
                "pair), so the cut has to happen inside the arms, with the condition carried as a "
                "residual -- the backward half then picks the arm the forward half took by reading "
                "the same condition. Each arm is entered and described (`WALK`), its yield is read "
                "off the Fill that writes the pair (`ARM`), and the backward half copies the arm's "
                "body into its own region and joins that arm's half of the pair -- but the arm is "
                "not *chosen* yet: the condition picks it, and reading that condition to pick is "
                "the next cut, not this one");
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Branch) << " exit=Join"
               << " condition=" << condition << " why=" << refusal.refusedWhy << '\n';
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Branch));
            return refusal;
        }
    } break;
    case ExitShape::Loop: {
        // **Defensive, and unreachable.** Two shapes answer `ExitShape::Loop`, and neither reaches
        // this cell: a LOOP node whose type is the lifted pair is cut above, before
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
            refusal,
            function,
            exit,
            "the exit",
            "this graph's exit is a Join that holds the pair and no branch names it, so the two "
            "halves' values meet at a loop -- a loop's residual is what one turn hands the next "
            "(design 4.2), and a half's walk does not enter a region yet");
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Loop) << " exit=Join"
           << " why=" << refusal.refusedWhy << '\n';
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Loop));
        return refusal;
    }
    case ExitShape::NotAPair: {
        // **Reachable from `.cml`**, not defensive. `split`'s own type refuses a plain function
        // before this function runs -- `Invalid call to 'split' with argument types <> ((x: float)
        // => float pure)` -- so a graph built by hand is one way in; a lifted graph is the other,
        // and it is the way a program takes. The shape: the exit node is the *application* whose
        // lifted copy is the pair (`kind=10`
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
        // around it in the library -- and the first is taken: an exit's shape is the lowering's
        // product, while this switch is the semantic invariant, and a criterion naming more shapes
        // is harder to state than a product that writes the one shape.
        SplitGraph refusal;
        refuseAt(
            refusal,
            function,
            exit,
            "the exit",
            "a lifted function answers with the pair (result, tangent); here the pair is a call's "
            "result -- the differentiated lambda ends in a call to a `with`-parameterised function "
            "-- and not the two-member tuple fill this cut takes its two halves from, so there is "
            "no pair to cut. This shape is not accepted: an exit's shape is the lowering's "
            "product, while this cut is the semantic invariant, and a criterion naming more "
            "shapes is harder to state than a product that writes the one shape");
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Exit) << " (" << refusal.refusedWhy
           << ")\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Exit));
        return refusal;
    }
    }
    // The two sides of the answer, read from the lifted type: the pair (value, tangent), or a tuple
    // of member lifts whose value members give their two legs to the two halves and whose function
    // member belongs to the forward half alone (`exitHalvesOf`).
    const ExitHalves answerHalves =
        exitHalvesOf(*function, exit, function->funcType()->exitType());
    if (!answerHalves.ok()) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Exit) << " (" << answerHalves.why << ")\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Exit));
        return SplitGraph{};
    }

    const std::span<const gc_node_ref_t> sourcePorts = function->normPorts();
    if (sourcePorts.size() != function->funcType()->normTypesCount()) {
        os << "REINTERP split function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Ports) << " (" << sourcePorts.size()
           << " ports for " << function->funcType()->normTypesCount() << " parameters)\n";
        noteRefusal(RefusalReading("split", function->name(), RefusalCause::Ports));
        return SplitGraph{};
    }

    const std::string base = "split_of_" + std::string(function->name());
    rt::GraphDraftGroup group(context, program);
    // Both members are reserved before either is built: they are the two graphs of one call, and
    // the first reserved one is the group's root (the forward half, which a caller runs first).
    const GraphId forwardId  = group.reserve(base + "_forward", base + "_forward");
    const GraphId backwardId = group.reserve(base + "_backward", base + "_backward");

    // **The cuts this one's value half needs, and the table it reads them from.** A call to a
    // callee that answers the pair is answered by that callee's own forward half, so the callee's
    // cut has to exist -- and the id has to be known before either half here is built, which is
    // what a body that calls itself needs: the entry for this graph is in the table already, and a
    // self-call names the graph this very build is writing. The table lives only for the length of
    // one transform call: it is not part of any graph and nothing in the core knows about it
    // (the same shape `jvp` uses for its own recursion).
    std::unordered_map<const rt::GCGraph *, CutMember> calleeCuts;
    // **The programs whose graphs this build stands on.** A nested build's two halves are graphs
    // its own program owns, so adopting that program brings them. One entry per direct callee cut,
    // in the order they were taken and without repeats: `Program::adoptMissing` binds what the
    // program it reads hands on and skips what is already bound, so the order says which program
    // answers for an id and the repeat would only bump a version.
    std::vector<rt::ProgramHold> nestedPrograms;
    const auto remember = [&nestedPrograms, &context](const SplitGraph &nested) {
        const rt::Program *standsOn = nested.program.get();
        if (standsOn == nullptr) {
            return;
        }
        const bool known = std::any_of(
            nestedPrograms.begin(),
            nestedPrograms.end(),
            [standsOn](const rt::ProgramHold &held) { return held.get() == standsOn; });
        if (!known) {
            nestedPrograms.emplace_back(*context, standsOn);
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
            const SplitGraph nested =
                buildSplitGraph(context, program, const_cast<rt::GCGraph *>(callee), os);
            if (nested.forward == nullptr || !nested.program) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Callee) << " (the cut of `"
                   << callee->name() << "`, which this body calls, "
                   << "does not build: "
                   << (nested.refusedWhy.empty() ? "no reason was given" : nested.refusedWhy)
                   << ")\n";
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Callee));
                return SplitGraph{};
            }
            CutMember member;
            member.forwardId = nested.forward->id();
            member.answer    = nested.forward->funcType() != nullptr
                                   ? nested.forward->funcType()->exitType()
                                   : nullptr;
            if (member.answer == nullptr) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Callee) << " (the forward "
                   << "half of `" << callee->name() << "` declares no exit type)\n";
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Callee));
                return SplitGraph{};
            }
            // **A cut of a cut.** The forward half of `callee` that this build calls is not the
            // whole answer when that half is itself answered by a call of *its* callee's forward
            // half: the nested build made that second cut, and its two halves are graphs its own
            // program holds. The graphs of the second callee's cut are in that program as well, and
            // as **taken** graphs rather than added ones -- `Program::adoptMissing` carries `added
            // ∪ taken`
            // (`Program::bindTaken`), so adopting the nested program hands on the whole
            // chain however deep it goes. That is why one entry per direct callee cut is enough
            // here, and why `SplitGraph` carries no transitive list: `adoptMissing` is what makes a
            // call to a grandchild's half resolve (`runtimeTarget != nullptr`, "a direct call names
            // a function whose graph is not in the program's graph table" was the symptom of it not
            // doing so). `test/cases/runtime/adopt_chain/split_three_calls.cml` is the case that
            // shows it.
            calleeCuts.emplace(callee, member);
            // The callee's cut itself: what that program took travels with it.
            remember(nested);
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
               << " skip=" << refusalCauseName(RefusalCause::Forward)
               << " (a `with` parameter of the source has no type)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward));
            return SplitGraph{};
        }
        if (!addCapturePorts(forward, function)) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Forward)
               << " (the closure of the source does not name its captured values)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward));
            return SplitGraph{};
        }
        // The lifted list is two halves -- the values, then their tangents -- so the forward half
        // takes the value ports, one port per parameter value (`valuePortsOf`, read from the
        // types).
        for (size_t i : valuePortsOf(function->funcType())) {
            Type *type = function->funcType()->normTypeAt(i);
            if (type == nullptr) {
                continue;
            }
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            forward.portOf[i] = port;
        }
        // The answer's forward members, in the order the lifted type writes them (`exitHalvesOf`):
        // the value leg of every value member and every function member as it stands. What the
        // forward half answers is those, made into the tuple the source's answer has without its
        // tangents -- one member is that member, and several are a tuple, which is the same shape
        // `splitHalvesWith` typed this half with.
        std::vector<gc_node_ref_t> answerMembers;
        answerMembers.reserve(answerHalves.forward.size());
        for (gc_node_ref_t root : answerHalves.forward) {
            if (!forward.copyHalf(root, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
            answerMembers.push_back(forward.copyOf[root]);
        }
        const auto *forwardExitType = tt::as_ptr<TupleType>(halves.forward->exitType());
        Type *answerType = forwardExitType != nullptr && forwardExitType->size() > 0
                               ? const_cast<Type *>(forwardExitType->typeAt(0))
                               : nullptr;
        gc_node_ref_t answerValue = rt::kInvalidNodeRef;
        if (answerMembers.size() == 1 && answerType != nullptr) {
            answerValue = answerMembers.front();
        } else if (answerType != nullptr) {
            answerValue = fillTuple(draft, answerType, answerMembers);
        }
        if (answerValue == rt::kInvalidNodeRef) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Forward)
               << " (the answer's forward members are not a value this half can hold)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward));
            return built;
        }
        // A call whose **linear half** crosses the cut is made by this half even when nothing on
        // the result side reads the value it answered: `lin` only exists where the call was made,
        // and it is the thing the second half calls. So the walk is extended to it, values and all.
        for (const Residual &residual : graphResiduals) {
            if (!residual.linearHalf || residual.node >= function->nodeCount() ||
                forward.copyOf[residual.node] != rt::kInvalidNodeRef) {
                continue;
            }
            if (!forward.copyHalf(residual.node, built)) {
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
        // The statements the source's exit gate waits on. They are not data for either side of the
        // cut, so neither walk above reaches them: the number would be right and the print the
        // body wrote would never run. The forward half is the side that runs the primal body, so
        // the statements come with it and its exit is a gate that waits on them, which is the
        // shape the source had (`camel/runtime/graph.h#gateWaitsOf`). Dropping them is a wrong
        // answer and not a refusal: a differentiated function that printed once would be silent. A
        // statement the plumbing cannot copy -- one with a control edge, or one whose place on a
        // chain cannot be reproduced -- is still refused by name inside the walk, never dropped.
        std::vector<gc_node_ref_t> exitWaits;
        for (gc_node_ref_t wait : gateWaitsOf(*function, function->exitNodeRef())) {
            if (wait >= function->nodeCount()) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " (the exit waits on a node that is not a node of the graph)\n";
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward));
                return built;
            }
            if (forward.copyOf[wait] == rt::kInvalidNodeRef && !forward.copyHalf(wait, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Forward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Forward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
            exitWaits.push_back(forward.copyOf[wait]);
        }
        // The residuals: the values `residualsOf` named, in its order -- the forward half computes
        // them all (they are computed before the cut, that is what makes them residuals) and hands
        // them on inside the `lin` it builds. A parameter's value is one of them when the backward
        // half reads it; the identity graph reads nothing, so `lin` captures nothing.
        std::vector<gc_node_ref_t> residualMembers;
        residualMembers.reserve(graphResiduals.size());
        for (const Residual &residual : graphResiduals) {
            if (residual.linearHalf) {
                // The second component of what this half's call answered: the callee's own linear
                // half, closed over whatever its body reads. The call site never sees those values,
                // and this projection is the whole of what it takes to hand them on.
                const gc_node_ref_t call = residual.node < function->nodeCount()
                                               ? forward.copyOf[residual.node]
                                               : rt::kInvalidNodeRef;
                if (call == rt::kInvalidNodeRef) {
                    os << "REINTERP split function=" << function->name()
                       << " skip=" << refusalCauseName(RefusalCause::Residual)
                       << " (the forward half does not make the call whose linear half "
                          "the cut carries)\n";
                    noteRefusal(RefusalReading("split", function->name(), RefusalCause::Residual));
                    return built;
                }
                const gc_node_ref_t lin = draft.addAccsNode(residual.type, 1, 0);
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
                // receives that value at the call (`addWithPorts`), and that port is what crosses
                // in the closure -- nothing is computed for it, so no walk reaches it unless the
                // result side happens to read the same weight.
                const auto withPort = forward.withPortOf.find(residual.node);
                if (withPort != forward.withPortOf.end()) {
                    value = withPort->second;
                }
            }
            if (value == rt::kInvalidNodeRef) {
                // A capture the backward half reads and the result side does not: same road, one
                // port further on -- the forward half's closure port for it (`addCapturePorts`) is
                // the value that crosses, read out of the function value the operator handed over.
                const auto captured = forward.capturePortOf.find(residual.node);
                if (captured != forward.capturePortOf.end()) {
                    value = captured->second;
                }
            }
            if (value == rt::kInvalidNodeRef) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Residual)
                   << " (the forward half does not compute a value the cut carries)\n";
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Residual));
                return built;
            }
            residualMembers.push_back(value);
        }
        // `lin`: the backward graph closed over those values, built here because here is where they
        // exist. Its closure layout is the one this build read from the backward graph, so the
        // collector scans it by exactly the type the graph states.
        const gc_node_ref_t linValue = buildLinearClosure(
            draft,
            program,
            backwardId,
            halves.closure,
            halves.backward,
            std::span<const gc_node_ref_t>(residualMembers.data(), residualMembers.size()));
        const std::array<gc_node_ref_t, 2> forwardExit{answerValue, linValue};
        const gc_node_ref_t out = fillTuple(
            draft,
            halves.forward->exitType(),
            std::span<const gc_node_ref_t>(forwardExit.data(), forwardExit.size()));
        // The exit: the pair, and -- when the body wrote statements the source's exit waited on --
        // a gate over it that waits on the same statements, in the same order. A body that printed
        // once prints once here too, because a call of this half calls the lift's forward side
        // once.
        gc_node_ref_t exit = out;
        if (!exitWaits.empty()) {
            std::vector<gc_node_ref_t> gateInputs;
            gateInputs.reserve(exitWaits.size() + 1);
            gateInputs.push_back(out);
            gateInputs.insert(gateInputs.end(), exitWaits.begin(), exitWaits.end());
            Type *gateType = halves.forward->exitType();
            const gc_node_ref_t gate =
                draft.addGateNode(gateType, draft.allocateRuntimeSlot(gateType));
            draft.setNormInputs(gate, gateInputs);
            exit = gate;
        }
        draft.setExitNode(exit);
        draft.setOutputNode(exit);
        draft.setReturnNode(exit, rt::GCReturnKind::Self);
    }
    {
        // The backward half: the tangent of every parameter in -- the residuals arrive by its
        // closure ports, a `with` value among them -- and the tangent out. No `with` list is
        // declared here, so there are no `with` ports to make either: a
        // read of one is a read of the closure port `addResidualCaptures` gave it.
        auto &draft = group.transaction(backwardId).edit();
        draft.setFuncType(halves.backward);
        std::unordered_map<gc_node_ref_t, size_t> residualIndexOf;
        for (size_t i = 0; i < graphResiduals.size(); ++i) {
            residualIndexOf[graphResiduals[i].node] = i;
        }
        Half backward(draft, function, program, true, &graphResiduals, &residualIndexOf);
        addResidualCaptures(draft, graphResiduals, backward);
        // The tangent ports: every ordinary port that is not a value port (`valuePortsOf`) and whose
        // type is a tangent space. A `()` slot is an answer this half gives, never an argument it
        // takes.
        std::vector<char> isValuePort(sourcePorts.size(), 0);
        for (size_t i : valuePortsOf(function->funcType())) {
            if (i < isValuePort.size()) {
                isValuePort[i] = 1;
            }
        }
        for (size_t i = 0; i < sourcePorts.size(); ++i) {
            if (isValuePort[i] != 0) {
                continue;
            }
            Type *type = function->funcType()->normTypeAt(i);
            if (type == nullptr || !camel::core::tangentSpaceOf(type).hasSpace()) {
                continue;
            }
            const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(port);
            backward.portOf[i] = port;
        }
        // The answer's tangent members, in the order the lifted type writes them (`exitHalvesOf`):
        // one per value member of the answer, and none for a function member.
        std::vector<gc_node_ref_t> tangentMembers;
        tangentMembers.reserve(answerHalves.tangent.size());
        for (gc_node_ref_t root : answerHalves.tangent) {
            if (!backward.copyHalf(root, built)) {
                os << "REINTERP split function=" << function->name()
                   << " skip=" << refusalCauseName(RefusalCause::Backward)
                   << " construct=" << built.refusedConstruct
                   << " uri=" << (built.refusedUri.empty() ? "-" : built.refusedUri)
                   << " why=" << built.refusedWhy << '\n';
                noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward)
                                .atConstruct(built.refusedConstruct));
                return built;
            }
            tangentMembers.push_back(backward.copyOf[root]);
        }
        Type *tangentAnswerType = halves.backward->exitType();
        gc_node_ref_t out       = rt::kInvalidNodeRef;
        if (tangentMembers.size() == 1) {
            out = tangentMembers.front();
        } else if (tangentAnswerType != nullptr) {
            out = fillTuple(draft, tangentAnswerType, tangentMembers);
        }
        if (out == rt::kInvalidNodeRef) {
            os << "REINTERP split function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Backward)
               << " (the answer's tangent members are not a value this half can hold)\n";
            noteRefusal(RefusalReading("split", function->name(), RefusalCause::Backward));
            return built;
        }
        draft.setExitNode(out);
        draft.setOutputNode(out);
        draft.setReturnNode(out, rt::GCReturnKind::Self);
    }
    // The programs of the cuts this one calls: the graphs of those two halves join this build's
    // program, so the calls this half wrote resolve at run time. Adopted, not rebuilt -- the ids
    // the calls name are the graphs those builds made, and no value is recomputed. The list is the
    // direct callee cuts' own programs; whatever those programs took travels with them by adoption.
    for (const rt::ProgramHold &nested : nestedPrograms) {
        if (nested) {
            group.adopt(*nested);
        }
    }
    group.encode();
    const auto *forwardMade         = group.encoded(forwardId);
    const auto *backwardMade        = group.encoded(backwardId);
    built.residuals                 = graphResiduals.size();
    os << "REINTERP split function=" << function->name() << " forward=" << rt::toValue(forwardId)
       << " backward=" << rt::toValue(backwardId) << " type="
       << (forwardMade != nullptr && forwardMade->funcType() != nullptr
               ? forwardMade->funcType()->toString()
               : std::string("-"))
       << " residuals=" << built.residuals;
    // Printed only when the graph being cut carries the mark, so every other cut keeps the line it
    // had: `remat`'s own line says the property was set, and this one says what the cut did with
    // it.
    if (function->recomputesOnBackward()) {
        os << " recompute=1";
    }
    os << '\n';
    built.program  = rt::ProgramHold(*context, group.program());
    built.forward  = const_cast<rt::GCGraph *>(forwardMade);
    built.backward = const_cast<rt::GCGraph *>(backwardMade);
    // What this cut stands on, past the graphs `program` already holds: one program per direct
    // callee cut, each of which holds every graph it took. A caller one level out adopts every
    // program named here and, with it, the whole chain a call of this cut can reach.
    built.nestedPrograms = std::move(nestedPrograms);
    return built;
}

} // namespace camel::transforms
