// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose.h"

#include "jvp.h"
#include "rule_expand.h"

#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/tree.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace {

/// The sum a cotangent accumulates with is the sum of the type it lands in: two array cotangents
/// add element by element (`:tangent/add_arr`, design principles 9.9) and a number's add is
/// `:op/add_d`. Handing an array to `add_d` is the same lie as calling it a float -- it reads the
/// array as a number and takes the process down, which is what two element reads of one array did
/// (a sum of two reads was enough; one read was not). Every accumulation in this file asks this
/// question; a site that spells `:op/add_d` itself decided the answer without asking.
const char *addUriFor(const camel::runtime::GCGraph *function, gc_node_ref_t ref, Type *&addType) {
    const camel::runtime::GCNode *node = function->node(ref);
    const Type *type                   = node != nullptr ? node->dataType() : nullptr;
    const camel::core::TangentSpaceAnswer space =
        type == nullptr ? camel::core::TangentSpaceAnswer{} : camel::core::tangentSpaceOf(type);
    if (space.hasSpace() && space.space != nullptr &&
        space.space->code() == camel::core::type::TypeCode::Array) {
        addType = space.space;
        return ":tangent/add_arr";
    }
    addType = Type::Float64();
    return ":op/add_d";
}

/// The type a transposed function's cotangent input lives in: the cotangent of the result, which is
/// the result's tangent space. Every shape that reaches these lines has passed the `float` guard
/// above, so this answers `float` today -- but it *asks* the type system instead of restating the
/// guard, so widening the guard cannot leave a `float` where an array is promised (design principles
/// 9.9, and the drift `jvp`'s four sites were fixed for).
Type *cotangentTypeOfResultType(const Type *result) {
    if (result == nullptr) {
        return Type::Float64();
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(result);
    return space.hasSpace() && space.space != nullptr ? space.space : Type::Float64();
}

Type *resultCotangentType(const FunctionType *function) {
    return cotangentTypeOfResultType(function != nullptr ? function->exitType() : nullptr);
}

Type *resultCotangentType(const camel::runtime::GCGraph *function) {
    return cotangentTypeOfResultType(function != nullptr && function->funcType() != nullptr
                                         ? function->funcType()->exitType()
                                         : nullptr);
}

} // namespace

FunctionType *transposeSignature(
    FunctionType *function, const std::vector<bool> &participates,
    std::vector<size_t> *answersOf, const std::vector<size_t> *captures) {
    if (answersOf != nullptr) {
        answersOf->clear();
    }
    if (function == nullptr) {
        return nullptr;
    }
    if (function->withTypesCount() != 0 || function->normTypesCount() == 0) {
        return nullptr;
    }
    if (function->exitType() != Type::Float64()) {
        return nullptr;
    }
    // The law, as the author rules it (2026-11-02 11:30): `transpose(g)` treats **every parameter
    // of `g` as a linear input** and answers `(y-bar) => (one cotangent per parameter)`. So no
    // parameter of `g` that takes a cotangent is a value its caller passes, and a body that only
    // reads such parameters gives a transposed function that takes the cotangent of the result and
    // nothing else. Two things stand before that cotangent instead of being passed as themselves:
    // a parameter whose type has no tangent space (an `int`, a `bool`, a string) is a value the call
    // site still fills, and so is a value the body reads from outside; which parameters those are is
    // a fact of the body, read below from the function rather than from its type. A fixed value a
    // body needs is not a parameter: it is
    // captured by the function that is transposed -- `transpose((dx) => f_lin(x, dx))(y-bar)` -- and
    // that is the caller's spelling. What it buys is that the type of a function says nothing about
    // which half of a pair it came from, so no bit has to be carried and the operator resolver --
    // which sees a type and nothing else -- has every fact it needs.
    //
    // `participates` is the one exception, and it is a fact of a *call site* rather than of a type:
    // where a call records how many of its trailing arguments are tangents, those are the parameters
    // a cotangent is answered for and the rest are values the caller still passes. That is the shape
    // a rule's tangent function has -- `mul_tangent(x, w, dx, dw)` is linear in `(dx, dw)`, so the
    // cotangent of such a call is the cotangent of its last two arguments while its first two remain
    // values. Empty means every parameter participates, which is the ordinary transposition of an
    // entry.
    //
    // A parameter with no tangent space (an integer, a `bool`, a string) cannot be what a cotangent
    // is, and is read as a value: nothing enters or leaves its place in the answer. A function whose
    // parameters all lack a space has no transpose at all.
    const size_t count = function->normTypesCount();
    if (!participates.empty()) {
        // The participating-set form is a kernel's: a function of plain numbers, with one flag per
        // parameter. Anything else is refused here rather than answered with a signature whose
        // ports a call site cannot fill.
        if (participates.size() != count) {
            return nullptr;
        }
        for (size_t i = 0; i < count; ++i) {
            if (function->normTypeAt(i) != Type::Float64()) {
                return nullptr;
            }
        }
    }
    camel::core::type::param_vec_t params;
    std::vector<Type *> cotangentsOfParticipants;
    for (size_t i = 0; i < count; ++i) {
        Type *const parameter = function->normTypeAt(i);
        const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(parameter);
        const bool participatesHere =
            participates.empty() ? (space.hasSpace() && space.space != nullptr) : participates[i];
        if (!participatesHere) {
            // A value the transposed graph reads and answers nothing for. It keeps its place among
            // the parameters the caller passes, in the order they stand.
            params.emplace_back(parameter, false);
            continue;
        }
        if (!space.hasSpace() || space.space == nullptr) {
            // A parameter a cotangent is answered for and no cotangent can land on: the answer
            // would have a member with no type, so the signature is refused rather than written.
            return nullptr;
        }
        cotangentsOfParticipants.push_back(space.space);
        if (answersOf != nullptr) {
            answersOf->push_back(i);
        }
    }
    if (cotangentsOfParticipants.empty()) {
        return nullptr;
    }
    // The values the body asked for and the caller still has: each is a port of its own, of the
    // parameter's own type, standing before the cotangent. A parameter that already stands among the
    // value parameters above is reused, not added again -- the port is one per parameter, and the
    // call site's argument count has to match the ports it fills, not the set it was given.
    //
    // "Already stands" is the parameter's own place in the list, read from the same predicate the
    // list was built from: a parameter no cotangent is answered for is a value parameter. It is
    // *not* "some parameter of the same type stands": two `float` parameters are one type and two
    // places (`div_tangent(a, b, da, db)` has three values the caller passes for four parameters),
    // and a capture that reused another parameter's port would hand the body that parameter's number
    // and put two places where the caller fills one.
    if (captures != nullptr) {
        for (const size_t at : *captures) {
            if (at >= count) {
                return nullptr;
            }
            Type *const parameter = function->normTypeAt(at);
            const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(parameter);
            const bool answeredHere =
                participates.empty() ? (space.hasSpace() && space.space != nullptr) : participates[at];
            if (answeredHere) {
                params.emplace_back(parameter, false);
            }
        }
    }
    params.emplace_back(resultCotangentType(function), false);
    return FunctionType::create(
        camel::core::type::param_vec_t{}, params,
        camel::core::type::TupleType::create(cotangentsOfParticipants));
}

/// A loop whose body let a cotangent escape carries that cotangent as a state of its own.
/// the loop is built with a second state -- the running sum of what escaped -- and answers the
/// escaped value with the state's own cotangent plus that sum. Before this, the loop refuses exactly as it
/// did before the channel existed, which is what the A/B of the reading below flips: the same case,
/// the same line, `skip=state` instead of `carried=1`.

/// A tuple value has a cotangent slot by slot: reading one component of a tuple fills
/// that slot and leaves the rest zero, so a value built as a tuple can be differentiated through and a
/// loop that carries several states can be transposed at all. Before this, a component read drops its
/// cotangent as a read of a known value -- which is what the transform did before this channel, and
/// what the A/B of the cases below flips back to: everything tuple-shaped refuses with `skip=output`.

/// A value whose type has no tangent space is a zero of the derivative rather than a reason to
/// refuse: a cotangent that lands on such a value is dropped, and the reading says which value it
/// was and why -- an integer is a step function, constant between its jumps, so `float(int(3x))`
/// contributes nothing to the derivative of what it is added to, and `x + float(int(3x))` answers 1.
/// The type answers this (`camel/core/tangent_space.h`), not the node kind: the same rule covers the
/// booleans, the strings and a function value. Before this, an integer in a loop body stops the body's walk
/// where it stands -- which is what the transform did before this rule, and what the A/B of the case
/// below flips back to: `skip=state`, with no answer at all. (The switch is gone; the rule and the`r`n/// reading are what remain.)

/// A call's recorded tangent-argument count is read (`GCNode::tangentArgs`: how many of the
/// call's trailing arguments a rule's expansion said are tangents). A kernel call inside an arm is
/// transposed as the linear map it is -- the values are ports, the tangents are what the answer is a
/// cotangent of, and the call site passes the values and the cotangent only. Before this, the count is unread,
/// the call is a plain function of all its parameters, and the arm stops at the tangent that has no
/// value: `arm-stop region=1 node=7 arg=1 callee=autodiff::mul_tangent<float>`. The A/B of the two readings.

/// A callee that arrives as a *value* is named. `calleeIdOf` answers for a FUNC node -- a call
/// written by name; a function value put into a `let` and called is a DATA node holding a static
/// function, and `GCGraph::staticFunctionGraphOf` is the one entry that turns it into a graph id (or
/// says which check refused: not a data node, no static slot, not a function type, no function value,
/// no graph, not a static reference of this graph). Before this, that shape refuses with
/// `skip=call callee=none`, which is what the transform did before this entry existed -- the A/B of
/// `transpose_function_value_no_capture.cml` flips exactly this. a value with no capture names its
/// graph and transposes; a closure names its graph too, and then fails later, on the capture. (The`r`n/// switch that guarded this is gone; the entry it guarded is the one the cases read.)

/// What the graphs of one transpose share: one group (a callee's transpose is another member of
/// it, exactly as `jvp` lifts one graph per callee) and the callees already transposed, so that
/// two calls of one function call one graph.
struct TransposeState {
    camel::runtime::GraphDraftGroup &group;
    const camel::runtime::Program *program;
    std::unordered_map<uint32_t, GraphId> callees;
    /// The context whose rule table the transpose reads. `TransposeState` is where it belongs: it is
    /// what the build is handed (`buildTransposeGraph`), it is not a field of any value the language
    /// names, and the rule's right side is looked up by name in the table the context keeps.
    const camel::core::context::context_ptr_t &context;
    /// True inside a *probe*: the scratch build that discovers what a member needs, whose answer is
    /// thrown away with its draft (see `buildTransposeMember`). A probe never probes.
    bool inProbe = false;
    /// What each member needs, keyed by the graph whose member it is. A call site reads this before
    /// it computes the type it calls with: the capture ports are part of that type, so the set has to
    /// be known before the signature is written, not after the build that uses it.
    std::unordered_map<const camel::runtime::GCGraph *, std::vector<size_t>> calleeNeeds;
};

bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, const GraphId member,
    std::ostream &os, const std::vector<bool> &participates = {},
    std::vector<size_t> *needs = nullptr) {
    camel::runtime::GraphDraftGroup &group = state.group;
    if (state.program == nullptr || function == nullptr) {
        return false;
    }
    // What this member needs is discovered by asking: the body decides which parameters it reads by
    // asking for their values, and nothing in the type says which. The ask is itself a build, so it
    // runs in a draft of its own -- a scratch group, destroyed with the members reserved in it --
    // because the real member's id was reserved before this call and cannot be taken back: what must
    // not be written into it is exactly what a failed attempt would leave behind. The probe's log is
    // an `ostringstream` too: a member that succeeds must not print the requests of its own trial.
    if (!state.inProbe) {
        camel::runtime::GraphDraftGroup scratch(state.context, state.program);
        TransposeState probe{scratch, state.program, {}, state.context, true};
        std::ostringstream probeLog;
        const GraphId probeMember = scratch.reserve("transpose_probe", "transpose_probe");
        std::vector<size_t> found;
        (void)buildTransposeMember(probe, function, probeMember, probeLog, participates, &found);
        state.calleeNeeds[function] = found;
        if (!found.empty()) {
            os << "TRANSPOSE-PROBE function=" << function->name() << " needs=";
            for (size_t i = 0; i < found.size(); ++i) {
                os << (i == 0 ? "" : ",") << found[i];
            }
            os << '\n';
        }
    }
    // The graph being built takes the ports the walk fills, so it asks for the shape its ports can
    // honour: the plain one. A closed half answers `(ct) => (cotangents)` at the call site (the
    // resolver, which sees only the type) and this graph still takes the tangents, which is what
    // `same=no` reports until the graph side learns where their cotangents land.
    // The values the body asked for are capture ports of the member, and a port is a parameter of
    // the type the member is called with -- so the set has to be in hand before the signature is
    // written, not after the build that reads it. The probe above is the one that found it, and its
    // answer is read here rather than decided a second time.
    const std::vector<size_t> &capturesOf = state.calleeNeeds[function];
    FunctionType *signature =
        transposeSignature(function->funcType(), participates, nullptr, &capturesOf);
    if (signature == nullptr) {
        os << "TRANSPOSE function=" << function->name() << " skip=signature\n";
        return false;
    }
    const size_t nodeCount         = function->nodeCount();
    const size_t paramCount        = function->funcType()->normTypesCount();
    // Whether parameter `at` is one the transposition answers for. An empty set means every parameter
    // is: that is the ordinary transposition, where a parameter is a linear input and nothing the
    // caller passes (author, 2026-11-02 11:30). Only a parameter that cannot hold a cotangent -- an
    // integer, a `bool` -- stays outside it and is read as a value. A set that is not empty belongs to
    // a call site, and its flags are read as written.
    const bool partial = !participates.empty();
    const auto participatesOf = [&](size_t at) {
        if (partial) {
            return at < participates.size() && participates[at];
        }
        const Type *const type = at < paramCount ? function->funcType()->normTypeAt(at) : nullptr;
        return type != nullptr && camel::core::tangentSpaceOf(type).hasSpace();
    };
    std::vector<std::string> usedRules;
    std::vector<uint32_t> armRegionsBuilt;
    // The parameters whose *value* the body asked for and this member cannot hand over: a parameter a
    // cotangent is answered for has no port, so a rule's right side that reads it is refused. The
    // refusal stands -- nothing here answers with a number it does not have -- and what is new is
    // that the ask is recorded as well as printed, because the second pass needs the set: those are
    // the parameters the member will hold as capture ports, filled by the call site that can forward
    // them. Recorded at the ask, so a build that stops in one place still names everything it wanted.
    std::vector<size_t> wantedValues;
    // The answers leave by whichever exit the walk takes -- a member that needs values usually fails
    // on one of them -- so they are handed out by the destructor rather than at the return.
    struct NeedsOut {
        const std::vector<size_t> &from;
        std::vector<size_t> *to;
        bool released = false;
        ~NeedsOut() {
            if (to != nullptr && released) {
                *to = from;
            }
        }
    } needsOut{wantedValues, needs, false};
    needsOut.released = true;
    gc_node_ref_t result = rt::kInvalidNodeRef;
    std::function<gc_node_ref_t(gc_node_ref_t)> knownValue;
    {
        auto &draft = group.transaction(member).edit();
        draft.setFuncType(signature);
        // Three shapes reach this, and the type says which one it is: a function of one float, a
        // split half (the residuals as one tuple, the tangents after it), and a plain function of
        // several floats. The first is the single-value pullback; the second and third differ in
        // where the values the graph reads come from -- a residual tuple or the parameters
        // themselves.
        //
        // A fourth shape is the one `split` hands out now (author, 2026-11-02 08:30): the linear half
        // is the backward graph, closed over the residuals the cut carried, so its parameters are the
        // tangents and the values it cannot compute are **capture ports** of its own graph
        // (`function->closureNodes()`, one per residual, in slot order -- the same list the kernel read
        // when it built the function value). Those values are ports of the transposed graph in turn:
        // the transposed function is handed them when it is called, exactly as it is handed the
        // cotangent, and nothing in any type states how many there are.
        const auto closureOf      = function->closureNodes();
        const bool carriesClosure = !closureOf.empty();
        const bool bundlesResiduals =
            !carriesClosure && paramCount > 1 &&
            dynamic_cast<const camel::core::type::TupleType *>(function->funcType()->normTypeAt(0)) !=
                nullptr;
        std::vector<gc_node_ref_t> paramPorts;
        // A parameter a cotangent is answered for is **not** a port of the new graph: it is what the
        // answer is about, and the caller has no value for it to pass (author, 2026-11-02 11:30).
        // What the transposed graph cannot get from the caller it gets from the closure the
        // transposed function carries -- the values the cut captured -- and a body that needs a fixed
        // value is written with that value captured, which is the caller's spelling and no business
        // of this port list. So the parameters of an ordinary transposition take no ports at all,
        // and the transposed function's only port is the cotangent below.
        std::vector<gc_node_ref_t> portOfParam(paramCount, rt::kInvalidNodeRef);
        {
            for (size_t i = 0; i < paramCount; ++i) {
                if (participatesOf(i)) {
                    continue;
                }
                Type *const portType = function->funcType()->normTypeAt(i);
                const gc_node_ref_t port =
                    draft.addPortNode(portType, draft.allocateRuntimeSlot(portType));
                draft.appendNormPort(port);
                paramPorts.push_back(port);
                portOfParam[i] = port;
            }
        }
        // The values the body asked for and no port gives it are capture ports of the member: one
        // per parameter, of the parameter's own type, in the order the set names them -- which is
        // the order the signature wrote them in, after the value parameters and before the
        // cotangent. A parameter that already stands among the value ports is reused rather than
        // added a second time ("the port is one per parameter"), so the graph's ports and the
        // type's parameters stay one list and a call site fills them by position.
        std::unordered_map<size_t, gc_node_ref_t> capturePortOf;
        for (const size_t at : capturesOf) {
            if (at >= paramCount) {
                continue;
            }
            Type *const portType = function->funcType()->normTypeAt(at);
            gc_node_ref_t capture = portOfParam[at];
            if (capture == rt::kInvalidNodeRef) {
                capture = draft.addPortNode(portType, draft.allocateRuntimeSlot(portType));
                draft.appendNormPort(capture);
                paramPorts.push_back(capture);
                portOfParam[at] = capture;
            }
            capturePortOf[at] = capture;
        }
        // The cotangent input is the cotangent of the result, in the result's own tangent space --
        // the same answer the signature promises two functions up (`resultCotangentType`), so the
        // port and the parameter cannot drift apart.
        // The residuals, as capture ports of the transposed graph: one port per capture the linear
        // half declares, of the capture's own type, in the closure's order, and the closure tuple the
        // new graph carries is the one the old graph carried -- so the value a caller copies into
        // `transpose(lin)`'s closure lands in the slot that graph reads. Nothing about their number
        // crosses the signature: the count is read here, from the graph.
        std::vector<gc_node_ref_t> closurePorts;
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> closurePortOf;
        for (const gc_node_ref_t ref : closureOf) {
            const camel::runtime::GCNode *node = function->node(ref);
            const Type *declared                = node != nullptr ? node->dataType() : nullptr;
            Type *const type = declared != nullptr ? const_cast<Type *>(declared) : Type::Float64();
            const gc_node_ref_t port =
                draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendClosureNode(port);
            closurePorts.push_back(port);
            closurePortOf[ref] = port;
        }
        if (carriesClosure && function->closureType() != nullptr) {
            draft.setClosureType(const_cast<camel::core::type::TupleType *>(function->closureType()));
        }
        const gc_node_ref_t cotangentIn =
            draft.addPortNode(resultCotangentType(function),
                              draft.allocateRuntimeSlot(resultCotangentType(function)));
        draft.appendNormPort(cotangentIn);

        const auto primitiveOf = [&draft](Type *type, const char *uri, std::vector<gc_node_ref_t> inputs) {
            const gc_node_ref_t made = draft.addOperNode(type, nullptr, uri);
            draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
            return made;
        };
        const auto primitive = [&primitiveOf](const char *uri, std::vector<gc_node_ref_t> inputs) {
            return primitiveOf(Type::Float64(), uri, std::move(inputs));
        };
        // The sum a cotangent accumulates with is the sum of the type it lands in: two array
        // cotangents add element by element (`:tangent/add_arr`, design principles 9.9) and a
        // number's add is `:op/add_d`. Handing an array to `add_d` is the same lie as calling it a
        // float -- it reads the array as a number and takes the process down, which is what two
        // element reads of one array did (a sum of two reads was enough; one read was not).
        const auto addUriAndType = [&](gc_node_ref_t ref, Type *&addType) {
            return addUriFor(function, ref, addType);
        };
        // Every cotangent accumulation in this file goes through this one function: given what a
        // place holds already (invalid means nothing yet) and what arrives, it answers the value
        // that place holds afterwards. The sum is chosen by the tangent space of what is already
        // there, so an array accumulates with `:tangent/add_arr` and a number with `:op/add_d`;
        // nothing here spells an addition of its own.
        // TODO(the program this needs): no case in the tree reaches the array side of this yet. What
        // would reach it is a program where two cotangents land on the same array-valued node -- the
        // `cotangent[ref]` accumulation above, run a second time for one array -- so `into` holds an
        // array and the sum has to be `:tangent/add_arr`. The gap is a program shape, not an
        // assertion: there is nothing to assert until such a program exists.
        const auto accumulate = [&](gc_node_ref_t into, gc_node_ref_t node) -> gc_node_ref_t {
            if (into == rt::kInvalidNodeRef) {
                return node;
            }
            Type *addType            = Type::Float64();
            const char *const addUri = addUriFor(function, into, addType);
            return into == node ? primitiveOf(addType, addUri, {node, node})
                                : primitiveOf(addType, addUri, {into, node});
        };
        const auto zero = [&]() {
            return draft.materializeStaticValue(
                camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),
                rt::kGCNodeFlagConstant);
        };
        // The type a value's cotangent lives in, read from the value's type: the same question
        // `jvp` asks of a parameter (`camel/core/tangent_space.h`), asked here of a loop's state and
        // of a value that escaped it. A `float[]` state pulls back an array; a number pulls back a
        // number. Nothing in this file decides that by hand any more -- the port, the tuple member
        // and the component read all ask here.
        const auto cotangentTypeOf = [function](gc_node_ref_t ref, Type *fallback) {
            const camel::runtime::GCNode *node = function->node(ref);
            const Type *type                   = node != nullptr ? node->dataType() : nullptr;
            if (type == nullptr) {
                return fallback;
            }
            const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(type);
            return space.hasSpace() && space.space != nullptr ? space.space : fallback;
        };
        // The zero of the space a value's cotangent lives in: a number's is `0.0`, and an array's is
        // the array of zeros built from the value's own shape (`:tangent/zeros_arr`, the operator
        // the pullback already builds for the same purpose).
        const auto zeroOf = [&](gc_node_ref_t valueRef) -> gc_node_ref_t {
            const camel::runtime::GCNode *node = function->node(valueRef);
            const Type *type                   = node != nullptr ? node->dataType() : nullptr;
            const camel::core::TangentSpaceAnswer space =
                type == nullptr ? camel::core::TangentSpaceAnswer{}
                                : camel::core::tangentSpaceOf(type);
            if (space.hasSpace() && space.space != nullptr &&
                space.space->code() == camel::core::type::TypeCode::Array) {
                const gc_node_ref_t zeros =
                    draft.addOperNode(space.space, nullptr, ":tangent/zeros_arr");
                draft.setNormInputs(zeros, std::span<const gc_node_ref_t>(&valueRef, 1));
                return zeros;
            }
            return zero();
        };
        // A tuple of these values, as a node of the new graph, and the type that names it: the Fill
        // the builder writes for a state tuple (`builder_control.cpp`, `gir.loop.split`), so a loop
        // that carries more than one value is the shape every consumer of a loop already reads.
        // The types travel with the values: a state's cotangent can be an array, and a tuple that
        // names it `float` is a tuple whose members are not the things it holds -- the same lie as
        // telling an array sum it is a float.
        const auto tupleOf = [&](const std::vector<gc_node_ref_t> &parts,
                                 const std::vector<Type *> &partTypes) {
            Type *const tupleType = camel::core::type::TupleType::create(partTypes);
            ::Tuple *emptyTuple   = ::Tuple::create(parts.size(), mm::autoSpace());
            const gc_node_ref_t source = draft.materializeStaticValue(
                camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), tupleType);
            std::vector<size_t> slots;
            for (size_t i = 0; i < parts.size(); ++i) {
                slots.push_back(i);
            }
            const gc_node_ref_t tuple =
                draft.addFillNode(tupleType, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
            draft.setNormInputs(tuple, std::span<const gc_node_ref_t>(&source, 1));
            draft.setWithInputs(tuple, std::span<const gc_node_ref_t>(parts));
            return std::pair<gc_node_ref_t, Type *>{tuple, tupleType};
        };

        // The cotangent of a value in the original graph, as a node of the new one: `k summed
        // cotangents` when the value was used k times, which is the transpose of using it k times.
        std::vector<gc_node_ref_t> cotangent(nodeCount, rt::kInvalidNodeRef);
        std::vector<bool> hasCotangent(nodeCount, false);
        // A constant operand carries no cotangent, but its *value* is what a rule needs (`ct * c`).
        std::vector<gc_node_ref_t> constantOf(nodeCount, rt::kInvalidNodeRef);
        // A value the cotangent path does not carry but the transposed graph must reproduce: a
        // branch's condition is computed from the parameters, and the arm that ran is decided by
        // it. These nodes are copied forward, not transposed.
        std::vector<gc_node_ref_t> forwardOf(nodeCount, rt::kInvalidNodeRef);

        const auto isConstant = [function](gc_node_ref_t ref) {
            const camel::runtime::GCNode *node = function->node(ref);
            return node != nullptr && node->kind == rt::GCNodeKind::Data;
        };
        const auto constantValue = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (constantOf[ref] != rt::kInvalidNodeRef) {
                return constantOf[ref];
            }
            const camel::runtime::GCNode *node    = function->node(ref);
            const std::span<const slot_t> slots = function->staticSlots();
            const std::ptrdiff_t index          = -static_cast<std::ptrdiff_t>(node->dataIndex);
            if (node->dataIndex < 0 && index >= 0 && static_cast<size_t>(index) < slots.size()) {
                constantOf[ref] = draft.materializeStaticValue(
                    slots[static_cast<size_t>(index)], node->dataType(), rt::kGCNodeFlagConstant);
            }
            return constantOf[ref];
        };
        // A round's cotangent can land on a value the round only read: that cotangent has to
        // leave the loop, and this is where it is caught. `allowEscapes` is on while a LOOP's
        // body runs -- a branch arm has nowhere to put a capture, so there an outside target is
        // still a refusal.
        bool allowEscapes = false;
        // One escaped cotangent: the product it is (`from`, the node whose rule handed it out) and
        // the value it landed on. Both are named because the refusal below has to say which node
        // escaped -- a value the round only read is the only thing that may stand for it, and a
        // reader that has the count alone cannot tell that from a port that merely stands nearby.
        struct Escape {
            gc_node_ref_t cotangent = rt::kInvalidNodeRef;
            gc_node_ref_t from      = rt::kInvalidNodeRef;
        };
        std::unordered_map<gc_node_ref_t, Escape> escapes;
        // A tuple value's cotangent, slot by slot. A component read puts its cotangent in its own
        // slot and leaves the rest empty; the value that holds the tuple turns the empty slots into
        // zeros when the walk reaches it. Keyed by the value, not by the read, because two reads of
        // one tuple are two slots of one cotangent, not two cotangents.
        std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> tupleCotangents;
        // How many slots a tuple value has: a Fill knows its components, anything else has to be
        // asked its type.
        const auto tupleSlots = [&](gc_node_ref_t ref) -> size_t {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                return 0;
            }
            if (node->kind == rt::GCNodeKind::Fill) {
                return function->withInputsOf(ref).size();
            }
            const Type *type = node->dataType();
            return type != nullptr && type->code() == camel::core::type::TypeCode::Tuple
                       ? camel::core::type::tree::arity(type)
                       : 0;
        };
        // Whether a value can hold a cotangent at all. The type answers, through the same query `jvp`
        // asks of a parameter: a float (and a tuple or array of them) is its own tangent space, and
        // an integer, a boolean, a `string` or a function has none. A node with no type yet is given
        // the benefit of the doubt -- guessing "no space" would silently stop a chain that has one.
        const auto hasTangentSpace = [&](gc_node_ref_t ref) {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                return true;
            }
            const Type *type = node->dataType();
            return type == nullptr || camel::core::tangentSpaceOf(type).hasSpace();
        };
        // One line per value whose cotangent is zero because the value cannot hold one. Deduplicated:
        // a value read in three rounds is still one value contributing one zero, and a reading that
        // printed it three times would look like three different findings.
        std::vector<gc_node_ref_t> zeroedValues;
        const auto noteZero = [&](gc_node_ref_t target, gc_node_ref_t from) {
            if (std::find(zeroedValues.begin(), zeroedValues.end(), target) !=
                zeroedValues.end()) {
                return;
            }
            zeroedValues.push_back(target);
            const camel::runtime::GCNode *node = function->node(target);
            const Type *type                  = node != nullptr ? node->dataType() : nullptr;
            os << "TRANSPOSE function=" << function->name() << " contribution=0 node=" << target
               << " type=" << (type != nullptr ? type->toString() : std::string("?"))
               << " space=none from=" << from << '\n';
        };
        // Whether a cotangent landing on `target` is one the answer holds. An ordinary transposition answers
// for every value; a partial one answers only for its participating parameters, and a parameter
// outside the set is a value the graph reads, not an answer it gives.
        const auto wantsCotangent = [&](gc_node_ref_t target) {
            if (!partial || target >= nodeCount) {
                return true;
            }
            const camel::runtime::GCNode *node = function->node(target);
            if (node == nullptr || node->kind != rt::GCNodeKind::Port) {
                return true;
            }
            const auto ports = function->normPorts();
            for (size_t i = 0; i < ports.size(); ++i) {
                if (ports[i] == target) {
                    return participatesOf(i);
                }
            }
            return true;
        };
        const auto addCotangent = [&](gc_node_ref_t ref, gc_node_ref_t node) {
            // A cotangent no parameter asks for is not collected: with a participating set, the
            // parameters outside it are values the transposed graph reads, not answers it gives, and
            // the answer tuple has no slot for them. Dropped before the arithmetic is looked at -- the
            // coefficient of such a delivery would be the value of a participating parameter, which is
            // exactly what does not exist here.
            if (!wantsCotangent(ref)) {
                return;
            }
            // A value with no tangent space holds no cotangent: the type says so, and the reading
            // names the value rather than leaving the zero unexplained. This is the same question
            // `jvp` asks of a parameter, asked of a value in the middle of a graph.
            if (!hasTangentSpace(ref)) {
                noteZero(ref, ref);
                return;
            }
            if (!hasCotangent[ref]) {
                cotangent[ref]    = node;
                hasCotangent[ref] = true;
                return;
            }
            Type *addType = Type::Float64();
            const char *const addUri = addUriAndType(ref, addType);
            if (cotangent[ref] == node) {
                cotangent[ref] = primitiveOf(addType, addUri, {node, node});
                return;
            }
            cotangent[ref] = primitiveOf(addType, addUri, {cotangent[ref], node});
        };
        // One position's cotangent: the rule the index filed for this operator, its right side
        // expanded where the operator stood. What stands in the graph is the rule's own function of
        // the cotangent -- `mul_cotangent(c, ct)`, `neg_cotangent(ct)` -- so no law is written here
        // a second time, and the numbers are the library's (design principles 5.5: one writing).
        //
        // The rule answers one cotangent per name its left side writes, in that order -- a tuple --
        // and `position` is the name whose cotangent this is. The operands' values are handed over
        // in the left side's own order, because that is the order the right side binds them in: the
        // dividend stays the dividend and the divisor stays the divisor, whichever position is being
        // answered. Which of them the answer at `position` reads is the rule's own business -- the
        // expansion asks only for those -- so a position that needs nothing it does not have is
        // answered even where the walk carries no value for the other operand (a transposed loop
        // body's own state, or a divisor that is the differentiated value itself).
        const auto ruleCotangent = [&](const std::string &ruleName,
                                       std::span<const gc_node_ref_t> inputs, size_t position,
                                       gc_node_ref_t ct, std::string &why) -> gc_node_ref_t {
            const auto valueOf = [&](size_t slot) -> gc_node_ref_t {
                return slot < inputs.size() ? knownValue(inputs[slot]) : rt::kInvalidNodeRef;
            };
            return camel::transforms::expandRuleByName(
                *state.context, group, group.transaction(member), ruleName, valueOf, {ct}, position,
                why);
        };
        const auto noteRule = [&usedRules](const std::string &name) {
            if (std::find(usedRules.begin(), usedRules.end(), name) == usedRules.end()) {
                usedRules.push_back(name);
            }
        };
        // The condition of a branch, copied: the transposed graph decides its arms with the same
        // predicate the forward graph did, at the same values -- which is why the parameters of `f`
        // are parameters of the transposed function too.
        bool complete = true;
        std::function<gc_node_ref_t(gc_node_ref_t)> forward;
        forward = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (ref >= nodeCount) {
                return rt::kInvalidNodeRef;
            }
            if (forwardOf[ref] != rt::kInvalidNodeRef) {
                return forwardOf[ref];
            }
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                return rt::kInvalidNodeRef;
            }
            if (node->kind == rt::GCNodeKind::Data) {
                forwardOf[ref] = constantValue(ref);
                return forwardOf[ref];
            }
            if (node->kind == rt::GCNodeKind::Port) {
                // A capture the linear half declared: its value is the port the transposed graph
                // declares for it, and that is where it comes from -- not from a parameter, not from
                // a tuple the caller had to destructure.
                if (const auto capture = closurePortOf.find(ref); capture != closurePortOf.end()) {
                    forwardOf[ref] = capture->second;
                    return forwardOf[ref];
                }
                const auto &ports = function->normPorts();
                for (size_t i = 0; i < ports.size(); ++i) {
                    if (ports[i] != ref) {
                        continue;
                    }
                    // The port at position `i` of the original graph **is** parameter `i`: a graph has one port
                    // per parameter, in the order the parameters stand. It is `paramPorts` -- the ports of
                    // the *new* graph -- that holds one entry per value parameter only, so reading this
                    // place through that list named the wrong parameter whenever a value parameter
                    // stood before an answered one (`index_into(xs, i)`: port 0 is the array, and the
                    // list's own position 0 is the `int`).
                    const size_t at = i < paramCount ? i : paramCount;
                    // Every place below indexes the *parameter*, not the port. `portOfParam`,
                    // `capturePortOf` and `transposeSignature`'s capture list are all keyed that way --
                    // the last one through `funcType()->normTypeAt(at)` -- so the ask this walk records
                    // has to be a parameter index too, or the set the probe reports names places the
                    // consumer reads as different parameters. The two numbers agree for every graph a
                    // parameter of which is one norm port, which is the shape this walk assumes; writing
                    // `at` says which of the two lists the number belongs to, and keeps a port past the
                    // last parameter (`i >= paramCount`, which has no parameter to name) out of the set
                    // instead of recording it as one.
                    // A value the body asked for and the caller hands in as a capture port: the
                    // port stands where the parameter stands, so the member reads the value from
                    // it and nothing is asked of the caller's closure.
                    if (const auto capture = capturePortOf.find(at);
                        capture != capturePortOf.end()) {
                        forwardOf[ref] = capture->second;
                        return capture->second;
                    }
                    if (!bundlesResiduals) {
                        // A parameter that is not answered for stands as the port the caller fills.
                        // A participating one has no port at all -- it is what the answer is a
                        // cotangent of -- so nothing hands its value in, and a rule that needs it is
                        // refused by name rather than answered with a number: the fixed value is the
                        // caller's to capture, not this graph's to invent.
                        forwardOf[ref] =
                            at < portOfParam.size() ? portOfParam[at] : rt::kInvalidNodeRef;
                        if (forwardOf[ref] == rt::kInvalidNodeRef && i < paramCount &&
                            std::find(wantedValues.begin(), wantedValues.end(), at) ==
                                wantedValues.end()) {
                            wantedValues.push_back(at);
                            os << "TRANSPOSE-NEEDS function=" << function->name()
                               << " param=" << at << '\n';
                        }
                        return forwardOf[ref];
                    }
                    if (paramPorts.empty()) {
                        complete = false;
                        return rt::kInvalidNodeRef;
                    }
                    if (i == 0) {
                        // The residuals themselves: a value the backward half is handed.
                        forwardOf[ref] = paramPorts.front();
                        return forwardOf[ref];
                    }
                    // A tangent's own value is the (i-1)-th residual: that is the whole point of
                    // the residuals -- the value the pullback needs, carried to where it is needed.
                    const auto *bundle = static_cast<const camel::core::type::TupleType *>(
                        function->funcType()->normTypeAt(0));
                    const gc_node_ref_t part = draft.addAccsNode(
                        bundle->typeAt(i - 1), static_cast<uint32_t>(i - 1));
                    draft.setNormInputs(part, std::span<const gc_node_ref_t>(&paramPorts.front(), 1));
                    forwardOf[ref] = part;
                    return forwardOf[ref];
                }
                complete = false;
                return rt::kInvalidNodeRef;
            }
            if (node->kind == rt::GCNodeKind::Accs) {
                const auto *accs = function->nodeBodyAs<rt::GCAccsBody>(ref);
                const auto norm  = function->normInputsOf(ref);
                if (accs == nullptr || norm.empty()) {
                    complete = false;
                    return rt::kInvalidNodeRef;
                }
                const gc_node_ref_t tuple = forward(norm.front());
                if (tuple == rt::kInvalidNodeRef) {
                    return rt::kInvalidNodeRef;
                }
                const gc_node_ref_t part = draft.addAccsNode(node->dataType(), accs->value);
                draft.setNormInputs(part, std::span<const gc_node_ref_t>(&tuple, 1));
                forwardOf[ref] = part;
                return part;
            }
            if (node->kind == rt::GCNodeKind::Fill) {
                const auto *fill = function->nodeBodyAs<rt::GCFillBody>(ref);
                // The slots of a Fill live after its header, in the payload: the payload is what
                // has to be written, not the header alone, or the copy has no components.
                const std::vector<std::byte> fillPayload =
                    rt::makeFillPayload(fill->fillKind, std::vector<size_t>(fill->slots().begin(), fill->slots().end()));
                // A Fill is a value, so it needs a slot of its own: without one the loop reads its
                // bounds from whatever slot 0 happens to hold.
                const gc_node_ref_t made = draft.addFillNode(
                    node->dataType(), std::span<const std::byte>(fillPayload),
                    draft.allocateRuntimeSlot(node->dataType()));
                // A Fill's Norm input is the static template of the pair it builds: without it the
                // node has no components, and a loop reading its bounds finds none.
                std::vector<gc_node_ref_t> fillNorm;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    const gc_node_ref_t copied = forward(input);
                    if (copied == rt::kInvalidNodeRef) {
                        return rt::kInvalidNodeRef;
                    }
                    fillNorm.push_back(copied);
                }
                if (!fillNorm.empty()) {
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(fillNorm));
                }
                std::vector<gc_node_ref_t> with;
                for (gc_node_ref_t input : function->withInputsOf(ref)) {
                    const gc_node_ref_t copied = forward(input);
                    if (copied == rt::kInvalidNodeRef) {
                        return rt::kInvalidNodeRef;
                    }
                    with.push_back(copied);
                }
                if (!with.empty()) {
                    draft.setWithInputs(made, std::span<const gc_node_ref_t>(with));
                }
                forwardOf[ref] = made;
                return made;
            }
            if (node->kind == rt::GCNodeKind::Func) {
                // A call the cotangent path also needs as a *value* -- a tangent feeds the next
                // tangent, so the second rule needs the first rule's number -- is computed forward
                // as it stands: the same callee graph, its arguments computed forward. It is
                // transposed as well, where its own cotangent arrives; this is only the number.
                const GraphId calleeId = function->calleeIdOf(ref);
                if (!rt::isValid(calleeId)) {
                    complete = false;
                    return rt::kInvalidNodeRef;
                }
                std::vector<gc_node_ref_t> copied;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    const gc_node_ref_t argument = forward(input);
                    if (argument == rt::kInvalidNodeRef) {
                        return rt::kInvalidNodeRef;
                    }
                    copied.push_back(argument);
                }
                const gc_node_ref_t made = draft.addFuncNode(calleeId, node->dataType());
                if (!copied.empty()) {
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(copied));
                }
                draft.addDependency(calleeId);
                forwardOf[ref] = made;
                return made;
            }
            if (node->kind != rt::GCNodeKind::Oper) {
                complete = false;
                return rt::kInvalidNodeRef;
            }
            const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
            std::vector<gc_node_ref_t> inputs;
            for (gc_node_ref_t input : function->normInputsOf(ref)) {
                const gc_node_ref_t copied = forward(input);
                if (copied == rt::kInvalidNodeRef) {
                    return rt::kInvalidNodeRef;
                }
                inputs.push_back(copied);
            }
            const gc_node_ref_t made = draft.addOperNode(node->dataType(), nullptr, body->uri());
            if (!inputs.empty()) {
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
            }
            forwardOf[ref] = made;
            return made;
        };

        // The value a rule needs beside the cotangent: a constant read straight, anything else
        // through the residuals -- that is what the residuals were handed over for.
        knownValue = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (ref >= nodeCount) {
                return rt::kInvalidNodeRef;
            }
            if (isConstant(ref)) {
                return constantValue(ref);
            }
            return forward(ref);
        };
        // The exit's cotangent is the caller's: `transpose(f)(x, 1.0)` asks what `1.0` pulls back
        // to. It is seeded before the walk, because the walk starts there and goes backwards.
        const gc_node_ref_t oldExit = function->exitNodeRef();
        if (oldExit >= nodeCount) {
            os << "TRANSPOSE function=" << function->name() << " skip=exit\n";
            return false;
        }
        addCotangent(oldExit, cotangentIn);

        // One arm of a branch, or one round of a loop body: the region, the values inside it whose
        // cotangents are already known, and the ports where the chain stops -- the parameter for an
        // arm, the loop's own state ports for a round. Returns the cotangent each stop received.
        // A single seed and a single stop is the branch's case; a loop body that carries several
        // states hands out one cotangent per state and collects one per state, so the shape has to
        // be a list on both ends.
        bool armsOk = true;
        // What an arm hands back to the residual tuple. A value an arm reads out of the residuals has
        // no forward source inside the arm, so its cotangent has exactly one place to go: the slot it
        // was read from. That is a different statement from a zero inside the arm -- a zero would say
        // the value does not affect the answer, and it does (`x` is what `2x` is computed from). The
        // channel is kept per slot and printed when a cotangent is handed over, because the transposed
        // function does not answer with it: the residuals are the values the cut carried, not
        // parameters a caller differentiates, so this is a second-order quantity. Printing it is what
        // keeps the accounting visible instead of silent.
        std::unordered_map<uint32_t, gc_node_ref_t> residualBack;
        struct ArmSeed {
            gc_node_ref_t at        = rt::kInvalidNodeRef;
            gc_node_ref_t cotangent = rt::kInvalidNodeRef;
        };
        const auto runSeeded = [&](uint32_t oldRegion, const std::vector<ArmSeed> &seeds,
                                   const std::vector<gc_node_ref_t> &stops)
            -> std::vector<gc_node_ref_t> {
            std::vector<gc_node_ref_t> local(nodeCount, rt::kInvalidNodeRef);
            std::vector<bool> hasLocal(nodeCount, false);
            std::vector<gc_node_ref_t> portCotangents(stops.size(), rt::kInvalidNodeRef);
            const auto addLocal = [&](gc_node_ref_t ref, gc_node_ref_t node) {
                if (!hasLocal[ref]) {
                    local[ref]    = node;
                    hasLocal[ref] = true;
                    return;
                }
                local[ref] = accumulate(local[ref], node);
            };
            // The arm's chain ends at the parameter, which lives outside the arm's region: the
            // cotangent of that parameter for this arm is what the chain hands it. Anything else
            // from outside the region is a residual this step does not have.
            const auto deliver = [&](gc_node_ref_t target, gc_node_ref_t node, gc_node_ref_t from) {
                // A value the round read that cannot hold a cotangent contributes zero: the round's
                // answer does not depend on it in a differentiable way, so there is nothing to carry
                // out of the loop and no accumulator to grow. Dropped here rather than at the escape
                // collection, which would otherwise build a float sum for an integer.
                if (!hasTangentSpace(target)) {
                    noteZero(target, from);
                    return;
                }
                if (function->nodeRegion(target) == oldRegion) {
                    addLocal(target, node);
                    return;
                }
                for (size_t i = 0; i < stops.size(); ++i) {
                    if (stops[i] != target) {
                        continue;
                    }
                    portCotangents[i] = accumulate(portCotangents[i], node);
                    return;
                }
                // A residual the arm read: the cotangent goes back into the slot it came from, named
                // by the slot's number. Nothing inside the arm stands for it, so dropping it here
                // would be a claim about the value rather than a rule about the cotangent.
                if (const camel::runtime::GCNode *targetNode = function->node(target);
                    targetNode != nullptr && targetNode->kind == rt::GCNodeKind::Accs) {
                    const auto readFrom = function->normInputsOf(target);
                    const auto *body     = function->nodeBodyAs<rt::GCAccsBody>(target);
                    const bool onAResidualPort =
                        !readFrom.empty() &&
                        std::any_of(stops.begin(), stops.end(), [&](gc_node_ref_t stopRef) {
                            return stopRef == readFrom.front();
                        });
                    if (body != nullptr && onAResidualPort) {
                        auto &slot = residualBack[body->value];
                        slot       = accumulate(slot, node);
                        os << "TRANSPOSE function=" << function->name() << " residual-return slot="
                           << body->value << " region=" << oldRegion << '\n';
                        return;
                    }
                }
                if (allowEscapes) {
                    // The round read this value from outside: its cotangent leaves the loop
                    // through the capture channel, accumulated over the rounds.
                    auto &slot = escapes[target];
                    if (slot.cotangent == rt::kInvalidNodeRef) {
                        slot = Escape{node, from};
                    } else {
                        slot.cotangent = accumulate(slot.cotangent, node);
                    }
                    return;
                }
                // Named, not silent: a cotangent the arm cannot place -- a value from outside the
                // region that is neither a stop nor a residual the arm read -- is the one failure this
                // walk has no rule for, and the next reader needs to know which value it landed on.
                os << "TRANSPOSE function=" << function->name() << " arm-unplaced region="
                   << oldRegion << " target=" << target << " from=" << from << '\n';
                armsOk = false;
            };
            for (const ArmSeed &seed : seeds) {
                if (seed.at != rt::kInvalidNodeRef) {
                    addLocal(seed.at, seed.cotangent);
                }
            }
            for (gc_node_ref_t ref = static_cast<gc_node_ref_t>(nodeCount); ref-- > 0;) {
                if (function->nodeRegion(ref) != oldRegion || !hasLocal[ref]) {
                    continue;
                }
                const camel::runtime::GCNode *node = function->node(ref);
                if (node == nullptr) {
                    continue;
                }
                if (node->kind == rt::GCNodeKind::Port) {
                    for (size_t i = 0; i < stops.size(); ++i) {
                        if (stops[i] == ref) {
                            portCotangents[i] = local[ref];
                            break;
                        }
                    }
                    continue;
                }
                if (node->kind == rt::GCNodeKind::Func) {
                    // A call inside an arm. **The callee is transposed, not the call rewritten**: the
                    // other way -- having `split` leave the un-lifted call in the arm -- would change
                    // what the cut produces (the tangents are what the lifted call computes, and the
                    // cut's whole point is that the tangent half still calls the lift), while
                    // transposing here is the same move the main walk already makes at a call, so
                    // `transpose` stays the only place that knows how to transpose a call. The callee
                    // gets its own transposed member (once per callee, the same map the main walk
                    // fills), and the call site passes what that member takes: the arguments as the arm
                    // has them, then this node's cotangent.
                    const GraphId calleeId = function->calleeIdOf(ref);
                    camel::runtime::GCGraph *callee =
                        rt::isValid(calleeId) ? state.program->graph(calleeId) : nullptr;
                    if (callee == nullptr) {
                        os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                           << oldRegion << " node=" << ref << " kind=call callee=none\n";
                        armsOk = false;
                        return {};
                    }
                    // A **rule kernel** call -- `mul_tangent(x, w, dx, dw)` -- is linear in its
                    // tangents, so its transposition answers for the tangents only and takes the values
                    // as ports. Which arguments are tangents is **the rule's** fact, recorded on this
                    // node when `reinterp` expanded the rule that produced it (`GCNode::tangentArgs`,
                    // `addFuncNode`'s last argument): the values are the captures the rule bound,
                    // the tangents are the lambda's own parameters. The rule is the one source;
                    // nothing here re-derives the split, and there is no second table to keep in step
                    // when a rule is added.
                    const uint16_t tangentArgs =
                        function->callTangentArgsOf(ref);
                    // The values a call inside an arm takes: a parameter (a stop), a residual projection, a
                    // constant, or a value the arm computes from those -- the shapes a cut half's arm
                    // has. A shape none of them covers still stops the arm by name.
                    // `std::function` rather than `auto`: a value the arm computes is forwarded by
                    // rebuilding the operation from what its own inputs forward to, so the lambda
                    // answers itself -- the same shape `forward` has, and for the same reason.
                    std::function<gc_node_ref_t(gc_node_ref_t)> forwardInArm;
                    forwardInArm = [&](gc_node_ref_t value) -> gc_node_ref_t {
                        const camel::runtime::GCNode *sourced = function->node(value);
                        if (sourced == nullptr) {
                            return rt::kInvalidNodeRef;
                        }
                        if (sourced->kind == rt::GCNodeKind::Port) {
                            // A capture the linear half declared is a port as well, and the graph being
                            // built already stands a port where it stands: `closurePortOf`, read first
                            // here for the same reason `forward()` reads it first. An arm of a cut half
                            // reads the values the cut carried -- `(x) => x*x`'s `x` is a residual, not
                            // a parameter -- so without this entry the arm's walk stops on the first
                            // parameter a callee's rule needs a value for: `arm-call region=1
                            // callee=autodiff::mul_tangent<float>`, then `arm-stop ... arg=0`, then
                            // `skip=arm arm=0`.
                            if (const auto capture = closurePortOf.find(value);
                                capture != closurePortOf.end()) {
                                return capture->second;
                            }
                            // The port at position `i` of the original graph **is** parameter `i`, the
                            // same rule `forward()` reads: a graph has one port per parameter, in the
                            // order the parameters stand. The list to read it through is
                            // `portOfParam`, indexed by parameter; `paramPorts` holds one entry per
                            // *value* parameter only, so position `i` in it named the wrong
                            // parameter as soon as a value parameter stood before an answered one.
                            const auto ports = function->normPorts();
                            for (size_t i = 0; i < ports.size(); ++i) {
                                if (ports[i] != value) {
                                    continue;
                                }
                                return i < portOfParam.size() ? portOfParam[i]
                                                              : rt::kInvalidNodeRef;
                            }
                            return rt::kInvalidNodeRef;
                        }
                        if (sourced->kind == rt::GCNodeKind::Data) {
                            return constantValue(value);
                        }
                        if (sourced->kind == rt::GCNodeKind::Accs) {
                            const auto *body = function->nodeBodyAs<rt::GCAccsBody>(value);
                            if (body != nullptr && !paramPorts.empty()) {
                                const gc_node_ref_t madePart =
                                    draft.addAccsNode(sourced->dataType(), body->value);
                                const gc_node_ref_t holder = paramPorts.front();
                                draft.setNormInputs(
                                    madePart, std::span<const gc_node_ref_t>(&holder, 1));
                                return madePart;
                            }
                        }
                        if (sourced->kind == rt::GCNodeKind::Oper) {
                            // A value the arm *computes* and hands to a call: `merged(x) = 2x + 5x`
                            // lifts to a sum whose rule binds the two summands, so the arm's
                            // `add_tangent(2x, 5x, dx, dc)` is passed two numbers that are not
                            // parameters -- and `add_tangent`'s transposed half still takes them as
                            // ports, because they are parameters it answers nothing for. The same
                            // move `forward()` makes for an operator answers it: the operation is
                            // rebuilt in the graph being built from the values its own inputs
                            // forward to, recursively, and nothing is invented -- an input the arm
                            // cannot forward stops the rebuild where it stops.
                            const auto *oper = function->nodeBodyAs<rt::GCOperBody>(value);
                            if (oper != nullptr) {
                                std::vector<gc_node_ref_t> operands;
                                bool allForwarded = true;
                                for (gc_node_ref_t input : function->normInputsOf(value)) {
                                    const gc_node_ref_t copied = forwardInArm(input);
                                    if (copied == rt::kInvalidNodeRef) {
                                        allForwarded = false;
                                        break;
                                    }
                                    operands.push_back(copied);
                                }
                                if (allForwarded) {
                                    const gc_node_ref_t rebuilt =
                                        draft.addOperNode(sourced->dataType(), nullptr, oper->uri());
                                    if (!operands.empty()) {
                                        draft.setNormInputs(
                                            rebuilt, std::span<const gc_node_ref_t>(operands));
                                    }
                                    return rebuilt;
                                }
                            }
                        }
                        return rt::kInvalidNodeRef;
                    };
                    const auto callInputs = function->normInputsOf(ref);

                    // A lifted call is transposed **through the graph it was lifted from**. The
                    // tangent half of a lift is linear in the tangents, so the cotangent of tangent i
                    // is the primal's cotangent of value i -- and that is computed from the values
                    // only. This is not a shortcut but the requirement: the transposed graph does not
                    // have a tangent's value (tangents are what the answer is a cotangent of), so a
                    // transposition that needed one could never be called. `jvp` records the graph a
                    // lift came from (`kGraphRefLiftedFrom`), which is the same record `split` reads
                    // to retarget a call.
                    const GraphId primalId = callee->liftedFrom();
                    camel::runtime::GCGraph *calleeToTranspose = callee;
                    // The parameters the transposition answers for. Empty = every parameter, which is
                    // what a lifted call and a plain call need; a kernel answers for its tangents.
                    std::vector<bool> participates;
                    size_t valueCount = callee->funcType()->normTypesCount();
                    size_t tangentBase = 0;
                    size_t answerCount = valueCount;
                    // Which argument each member of the callee's answer is the cotangent of, as a
                    // position among the call's normal inputs; and which arguments are values the
                    // member reads. Both follow from the same predicate the member's signature was
                    // built from (author, 2026-11-02 11:30): an answered parameter is passed no value
                    // and answers one cotangent, and the two lists are the parameters' places.
                    std::vector<size_t> answerAt;
                    std::vector<size_t> valueAt;
                    if (tangentArgs > 0) {
                        const size_t calleeParams = callee->funcType()->normTypesCount();
                        os << "TRANSPOSE function=" << function->name() << " arm-call region="
                           << oldRegion << " node=" << ref << " callee=" << callee->name()
                           << " args=" << callInputs.size() << " tangent-args=" << tangentArgs << '\n';
                        if (tangentArgs > calleeParams) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref << " tangent-args=" << tangentArgs
                               << " params=" << calleeParams << '\n';
                            armsOk = false;
                            return {};
                        }
                        valueCount  = calleeParams - tangentArgs;
                        tangentBase = valueCount;
                        answerCount = tangentArgs;
                        participates.assign(calleeParams, false);
                        for (size_t i = valueCount; i < calleeParams; ++i) {
                            participates[i] = true;
                        }
                        for (size_t i = 0; i < valueCount; ++i) {
                            valueAt.push_back(i);
                        }
                        for (size_t i = 0; i < answerCount; ++i) {
                            answerAt.push_back(tangentBase + i);
                        }
                    } else if (rt::isValid(primalId)) {
                        camel::runtime::GCGraph *primal = state.program->graph(primalId);
                        const size_t primalParams =
                            primal != nullptr ? primal->funcType()->normTypesCount() : 0;
                        if (primal == nullptr || primalParams == 0 || valueCount != 2 * primalParams) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref << " lift=primal"
                               << " params=" << valueCount << '/' << primalParams << '\n';
                            armsOk = false;
                            return {};
                        }
                        calleeToTranspose = primal;
                        valueCount        = primalParams;
                        tangentBase       = primalParams;
                        answerCount       = primalParams;
                        // A primal's parameters are the values the lift was applied to, and every one
                        // of them is a linear input of the transposition: the member reads none of
                        // them and answers one cotangent per tangent (author, 2026-11-02 11:30).
                        for (size_t i = 0; i < answerCount; ++i) {
                            answerAt.push_back(tangentBase + i);
                        }
                    } else {
                        // An ordinary call: a parameter whose type can hold a cotangent is answered
                        // for and is passed no value; a parameter that cannot -- the `int` of
                        // `xs[i]` -- is a value the member reads. The list is the parameters' places,
                        // so an answered parameter standing before a value is written correctly.
                        const size_t calleeParams = callee->funcType()->normTypesCount();
                        answerCount = 0;
                        for (size_t i = 0; i < calleeParams; ++i) {
                            const camel::core::TangentSpaceAnswer space =
                                camel::core::tangentSpaceOf(callee->funcType()->normTypeAt(i));
                            if (space.hasSpace() && space.space != nullptr) {
                                answerAt.push_back(i);
                                ++answerCount;
                            } else {
                                valueAt.push_back(i);
                            }
                        }
                    }
                    std::vector<gc_node_ref_t> arguments;
                    arguments.reserve(valueAt.size());
                    for (const size_t at : valueAt) {
                        if (at >= callInputs.size()) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref << " arg=" << at
                               << " callee=" << callee->name() << '\n';
                            armsOk = false;
                            return {};
                        }
                        const gc_node_ref_t argument = forwardInArm(callInputs[at]);
                        if (argument == rt::kInvalidNodeRef) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref << " arg=" << callInputs[at]
                               << " callee=" << callee->name() << '\n';
                            armsOk = false;
                            return {};
                        }
                        arguments.push_back(argument);
                    }
                    if (callInputs.size() != valueCount + tangentBase) {
                        os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                           << oldRegion << " node=" << ref << " args=" << callInputs.size()
                           << " values=" << valueCount << '\n';
                        armsOk = false;
                        return {};
                    }
                    GraphId transposed   = rt::kInvalidGraphId;
                    const auto knownCall = state.callees.find(rt::toValue(calleeToTranspose->id()));
                    if (knownCall == state.callees.end()) {
                        transposed = group.reserve(
                            "transpose_of_" + std::string(calleeToTranspose->name()),
                            "transpose_of_" + std::string(calleeToTranspose->name()));
                        state.callees.emplace(rt::toValue(calleeToTranspose->id()), transposed);
                        if (!buildTransposeMember(
                                state, calleeToTranspose, transposed, os, participates)) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref
                               << " callee=" << calleeToTranspose->name() << '\n';
                            armsOk = false;
                            return {};
                        }
                    } else {
                        transposed = knownCall->second;
                    }
                    // The values the member asked for and its own parameters cannot give it: this
                    // arm's call has them among its arguments, one per index of the set, in the
                    // set's order -- the order the member's capture ports stand in -- and they are
                    // arguments of the member, not identities of the callee, so they join
                    // `arguments` and not `callArgs`. A value the arm cannot forward stops the arm
                    // exactly as one of the callee's values would: no partial capture.
                    for (const size_t idx : state.calleeNeeds[calleeToTranspose]) {
                        if (idx >= callInputs.size()) {
                            os << "TRANSPOSE function=" << function->name()
                               << " arm-stop region=" << oldRegion << " node=" << ref
                               << " arg=" << idx << " callee=" << calleeToTranspose->name()
                               << '\n';
                            armsOk = false;
                            return {};
                        }
                        const gc_node_ref_t captured = forwardInArm(callInputs[idx]);
                        if (captured == rt::kInvalidNodeRef) {
                            os << "TRANSPOSE function=" << function->name()
                               << " arm-stop region=" << oldRegion << " node=" << ref
                               << " arg=" << callInputs[idx]
                               << " callee=" << calleeToTranspose->name() << '\n';
                            armsOk = false;
                            return {};
                        }
                        arguments.push_back(captured);
                    }
                    // The callee's answers are cotangents, and the types they live in are the ones
                    // its transposed signature answers with -- the tuple `transposeSignature` built.
                    // Read from the function rather than restated as `float`: a callee that answers
                    // an array cotangent must not be read back as a number.
                    FunctionType *const calleeSignature = transposeSignature(
                        calleeToTranspose->funcType(), participates, nullptr,
                        &state.calleeNeeds[calleeToTranspose]);
                    Type *const calleeExit = calleeSignature != nullptr
                                                 ? calleeSignature->exitType()
                                                 : nullptr;
                    const auto *calleeAnswerMembers =
                        dynamic_cast<const camel::core::type::TupleType *>(calleeExit);
                    std::vector<Type *> answerComponentTypesStorage;
                    if (calleeAnswerMembers != nullptr &&
                        calleeAnswerMembers->size() == answerCount) {
                        answerComponentTypesStorage.assign(calleeAnswerMembers->types().begin(),
                                                           calleeAnswerMembers->types().end());
                    }
                    Type *const answerType =
                        answerCount == 1
                            ? (calleeExit != nullptr ? calleeExit : Type::Float64())
                            : (answerComponentTypesStorage.size() == answerCount
                                   ? calleeExit
                                   : camel::core::type::TupleType::create(
                                         std::vector<Type *>(answerCount, Type::Float64())));
                    const gc_node_ref_t made = draft.addFuncNode(transposed, answerType);
                    std::vector<gc_node_ref_t> callArgs   = arguments;
                    callArgs.push_back(local[ref]);
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
                    for (size_t i = 0; i < answerCount; ++i) {
                        // The primal's cotangent of value i is the cotangent of the tangent that
                        // carries it; for a plain call (no lift) it is the cotangent of argument i.
                        const gc_node_ref_t targetAt = callInputs[tangentBase + i];
                        if (isConstant(targetAt)) {
                            // A constant argument is a known value: it has no cotangent to hand, the
                            // same way the operator rules skip a constant operand (`mul_t` does).
                            continue;
                        }
                        if (answerCount == 1) {
                            deliver(targetAt, made, ref);
                            break;
                        }
                        const gc_node_ref_t part = draft.addAccsNode(
                            i < answerComponentTypesStorage.size()
                                ? answerComponentTypesStorage[i]
                                : Type::Float64(),
                            static_cast<uint32_t>(i));
                        draft.setNormInputs(part, std::span<const gc_node_ref_t>(&made, 1));
                        deliver(targetAt, part, ref);
                    }
                    continue;
                }
                if (node->kind == rt::GCNodeKind::Cast || node->kind == rt::GCNodeKind::Copy) {
                    // A conversion passes a cotangent along when both sides can hold one. A cast
                    // *to* an integer (`int(3x)`) ends the chain by its own type: the value it
                    // produces is a step function, and the cotangent of a step function's output is
                    // zero with respect to its input. A cast *from* one (`float(n)`) hands its
                    // cotangent to the integer, and the delivery above drops it.
                    const auto castInputs = function->normInputsOf(ref);
                    if (!hasTangentSpace(ref)) {
                        noteZero(ref, ref);
                        continue;
                    }
                    if (!castInputs.empty()) {
                        deliver(castInputs.front(), local[ref], ref);
                    }
                    continue;
                }
                if (node->kind != rt::GCNodeKind::Oper) {
                    // Named, not silent: an arm whose body holds something other than an operator
                    // with a transpose rule stops here, and the line says which node and which kind,
                    // so the next reader sees what shape the arm has (a lifted call is a FUNC) rather
                    // than only that the arm failed.
                    os << "TRANSPOSE function=" << function->name() << " arm-stop region=" << oldRegion
                       << " node=" << ref << " kind=" << static_cast<int>(node->kind) << '\n';
                    armsOk = false;
                    return {};
                }
                // The same rule as the caller's walk: a cotangent with nowhere to go stops at the
                // type. Inside a loop body the shape it stops is `s + float(n)` -- `float(n)` has one
                // operand and it is an integer, so the round's cotangent does not reach it, and the
                // loop needs no accumulator for it.
                {
                    if (!hasTangentSpace(ref)) {
                        noteZero(ref, ref);
                        continue;
                    }
                    bool anyReceiver = false;
                    for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                        if (!isConstant(operand) && hasTangentSpace(operand)) {
                            anyReceiver = true;
                            break;
                        }
                    }
                    if (!anyReceiver) {
                        for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                            if (!isConstant(operand)) {
                                noteZero(operand, ref);
                            }
                        }
                        continue;
                    }
                }
                const auto inputs = function->normInputsOf(ref);
                const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const auto rule  = camel::core::transposeRuleOf(std::string(body->uri()));
                if (!rule.has_value() || rule->status != camel::parse::ast::RuleLookup::Found) {
                    armsOk = false;
                    return {};
                }
                noteRule(rule->rule);
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (isConstant(inputs[i])) {
                        continue;
                    }
                    std::string why;
                    const gc_node_ref_t answer =
                        ruleCotangent(rule->rule, inputs, i, local[ref], why);
                    if (answer == rt::kInvalidNodeRef) {
                        armsOk = false;
                        os << "TRANSPOSE function=" << function->name()
                           << " skip=cotangent-needs-a-value uri=" << body->uri()
                           << " position=" << i << " reason=" << why << '\n';
                        return {};
                    }
                    deliver(inputs[i], answer, ref);
                }
                continue;
            }
            return portCotangents;
        };
        // A single seed and a single stop: the branch's case, which is what every arm of a branch
        // is. A loop body is the same walk with one seed and one stop per state it carries.
        const auto runArm = [&](uint32_t oldRegion, gc_node_ref_t armResult,
                                gc_node_ref_t armCotangent,
                                const std::vector<gc_node_ref_t> &stops) -> gc_node_ref_t {
            const std::vector<gc_node_ref_t> back =
                runSeeded(oldRegion, {ArmSeed{armResult, armCotangent}}, stops);
            return back.empty() ? rt::kInvalidNodeRef : back.front();
        };

        // The walk runs the graph backwards: node ids are creation order, so a node only reads ids
        // below its own, and the body's nodes are walked in reverse. A branch's arms are regions,
        // not ids, so they are walked by `runArm` when the branch is reached.
        for (gc_node_ref_t ref = static_cast<gc_node_ref_t>(nodeCount); ref-- > 0;) {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr || !hasCotangent[ref]) {
                continue;
            }
            if (function->nodeRegion(ref) != 0) {
                // An arm's nodes are the arm's business; walking them here would run both arms.
                continue;
            }
            switch (node->kind) {
            case rt::GCNodeKind::Port:
                // The answer is assembled below, one member per parameter the transposition answers
                // for. A parameter's cotangent is a member of that tuple and never the exit itself --
                // so even a function of one parameter answers a one-long tuple, which is the shape
                // the law states for every arity (author, 2026-11-02 11:30).
                break;
            case rt::GCNodeKind::Data:
                break;
            case rt::GCNodeKind::Accs: {
                // Reading one component of a tuple. The cotangent of the tuple is a value of the
                // same shape: this slot is what arrived here, and the other slots are zero. Filling
                // them is the whole point -- this case used to drop the cotangent, on the reading
                // that a component of the residuals is a known value, and that reading is why a
                // tuple had no cotangent at all and every function returning one refused.
                const auto norm  = function->normInputsOf(ref);
                const auto *body = function->nodeBodyAs<rt::GCAccsBody>(ref);
                if (body == nullptr || norm.empty() ||
                    function->nodeRegion(norm.front()) != 0) {
                    break;
                }
                const gc_node_ref_t holder = norm.front();
                // A tuple inside a tuple: the read this node takes a component of is itself read a
                // component at a time, so its cotangent is a tuple, not a number, and it belongs to
                // the value the slot holds rather than to a slot of the holder's own cotangent. A
                // tuple put where a component cotangent belongs is not a cotangent at all, and the
                // walk cannot see it: it answered a silent zero. So the sub-tuple's slots are handed
                // over to the component the slot names -- `t.0.1` gives the second slot of `t.0`'s
                // cotangent to `t.0`'s own value, which is a tuple written out in the body.
                const auto nested = tupleCotangents.find(ref);
                if (nested != tupleCotangents.end()) {
                    const auto *held  = function->node(holder);
                    const auto parts2 = function->withInputsOf(holder);
                    if (held == nullptr || held->kind != rt::GCNodeKind::Fill ||
                        body->value >= parts2.size()) {
                        os << "TRANSPOSE function=" << function->name()
                           << " skip=component node=" << ref << " holder=" << holder << '\n';
                        return false;
                    }
                    std::vector<gc_node_ref_t> &into = tupleCotangents[parts2[body->value]];
                    for (size_t j = 0; j < nested->second.size(); ++j) {
                        const gc_node_ref_t part = nested->second[j];
                        if (part == rt::kInvalidNodeRef) {
                            continue;
                        }
                        if (into.size() <= j) {
                            into.resize(j + 1, rt::kInvalidNodeRef);
                        }
                        into[j] = accumulate(into[j], part);
                    }
                    hasCotangent[parts2[body->value]] = true;
                    break;
                }
                std::vector<gc_node_ref_t> &slots = tupleCotangents[holder];
                const size_t count                = tupleSlots(holder);
                if (slots.size() < count) {
                    slots.resize(count, rt::kInvalidNodeRef);
                }
                if (body->value >= slots.size()) {
                    break;
                }
                gc_node_ref_t &slot = slots[body->value];
                slot = accumulate(slot, cotangent[ref]);
                hasCotangent[holder] = true;
                break;
            }
            case rt::GCNodeKind::Fill: {
                // The value that holds a tuple hands each slot to the component it belongs to. The
                // slots nobody read are zeros, not holes: a component that is not asked for pulls
                // back nothing, and saying so with a zero is the same statement a parameter no
                // cotangent reached makes below.
                const auto found = tupleCotangents.find(ref);
                if (found == tupleCotangents.end()) {
                    break;
                }
                const auto parts = function->withInputsOf(ref);
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (isConstant(parts[i])) {
                        continue;
                    }
                    const gc_node_ref_t part =
                        i < found->second.size() ? found->second[i] : rt::kInvalidNodeRef;
                    addCotangent(parts[i], part == rt::kInvalidNodeRef ? zero() : part);
                }
                break;
            }
            case rt::GCNodeKind::Cast:
            case rt::GCNodeKind::Copy: {
                const auto norm = function->normInputsOf(ref);
                if (!norm.empty()) {
                    addCotangent(norm.front(), cotangent[ref]);
                }
                break;
            }
            case rt::GCNodeKind::Gate: {
                const auto norm = function->normInputsOf(ref);
                if (!norm.empty()) {
                    addCotangent(norm.back(), cotangent[ref]);
                }
                break;
            }
            case rt::GCNodeKind::Join: {
                // The cotangent of a join is the cotangent of the chain downstream of it; which arm
                // produced the value decides which arm's chain receives it. Both arms are built,
                // each in its own region, and the join picks one -- so the two arms' cotangents are
                // joined, not added: only one of them ran.
                const gc_node_ref_t ctJoin             = cotangent[ref];
                const auto arms                        = function->withInputsOf(ref);
                const auto normBranch                  = function->normInputsOf(ref);
                if (arms.size() < 2 || normBranch.empty()) {
                    os << "TRANSPOSE function=" << function->name() << " skip=join arms="
                       << arms.size() << '\n';
                    return false;
                }
                const gc_node_ref_t join = draft.addJoinNode(
                    Type::Float64(), rt::kInvalidNodeRef, static_cast<rt::gc_cnt_t>(arms.size()));
                const gc_node_ref_t brch = draft.addBrchNode(
                    Type::Int64(), join, static_cast<rt::gc_cnt_t>(arms.size()));
                // The join's norm input is the branch; the branch's own norm input is the
                // condition this arm was chosen by.
                const auto conditionInputs = function->normInputsOf(normBranch.front());
                if (conditionInputs.empty()) {
                    os << "TRANSPOSE function=" << function->name() << " skip=condition\n";
                    return false;
                }
                const gc_node_ref_t condition = forward(conditionInputs.front());
                if (!complete || condition == rt::kInvalidNodeRef) {
                    os << "TRANSPOSE function=" << function->name() << " skip=condition\n";
                    return false;
                }
                // A condition is a choice, not a magnitude: the branch that ran is decided by it, and
                // the transposed graph copies it forward rather than differentiating it. So the
                // cotangent never reaches it, and the reading would say nothing about a value that is
                // in the middle of the graph unless it is said here -- a bool contributes zero because
                // it has no tangent space, and that is a fact about the derivative, not an omission.
                if (!hasTangentSpace(conditionInputs.front())) {
                    noteZero(conditionInputs.front(), normBranch.front());
                }
                draft.setNormInputs(brch, std::span<const gc_node_ref_t>(&condition, 1));
                std::vector<gc_node_ref_t> armCotangents;
                // Where an arm's chain stops. The answer of the transposed function is the cotangent of
                // the *tangents* the backward half is given, so an arm of a residual-bundle half stops at
                // its first tangent port -- not at the bundle, whose cotangent is a second-order quantity
                // (a value the cut carried, not a parameter a caller differentiates). The bundle is
                // still a stop, so a cotangent that lands on it is placed rather than unplaced: it is
                // collected and dropped, which is the statement that it contributes nothing to the
                // answer. A function that is not a residual bundle keeps the single stop it had.
                const auto &stopPorts = function->normPorts();
                std::vector<gc_node_ref_t> stops;
                if (carriesClosure) {
                    // The chain stops at the tangent the arm answers and at the captures it read: the
                    // captures are values the cut carried, not parameters a caller differentiates, so a
                    // cotangent that lands on one is collected and dropped -- the statement that it
                    // contributes nothing to the answer. One tangent is what an arm answers today, as
                    // for the residual bundle; more than one would have to answer a tuple per arm.
                    if (stopPorts.size() > 1) {
                        os << "TRANSPOSE function=" << function->name() << " skip=arm-tangents ports="
                           << stopPorts.size() << '\n';
                        return false;
                    }
                    if (!stopPorts.empty()) {
                        stops.push_back(stopPorts.front());
                    }
                    for (const gc_node_ref_t port : closurePorts) {
                        stops.push_back(port);
                    }
                } else if (bundlesResiduals && !stopPorts.empty()) {
                    if (stopPorts.size() > 2) {
                        // One tangent is what an arm of a cut half answers today; more than one would
                        // have to answer a tuple per arm, and nothing builds that yet.
                        os << "TRANSPOSE function=" << function->name() << " skip=arm-tangents ports="
                           << stopPorts.size() << '\n';
                        return false;
                    }
                    stops.push_back(stopPorts[1]);
                    stops.push_back(stopPorts.front());
                } else {
                    stops.push_back(stopPorts.front());
                }
                for (size_t k = 0; k < arms.size(); ++k) {
                    const uint32_t oldRegion = function->nodeRegion(arms[k]);
                    const uint32_t newRegion =
                        draft.addRegion(rt::GCRegionKind::Arm, 0, brch, static_cast<uint32_t>(k));
                    draft.setInsertRegion(newRegion);
                    const gc_node_ref_t armCotangent = runArm(oldRegion, arms[k], ctJoin, stops);
                    if (!armsOk || armCotangent == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=arm arm=" << k
                           << '\n';
                        return false;
                    }
                    draft.setRegionResult(newRegion, armCotangent);
                    draft.setInsertRegion(0);
                    armCotangents.push_back(armCotangent);
                    armRegionsBuilt.push_back(newRegion);
                }
                // The join names the branch and the branch names the join: the join was made first
                // and its payload is written now that the branch and the arms exist.
                camel::runtime::GCJoinBody joinBody{};
                joinBody.brch     = brch;
                joinBody.armCount = static_cast<rt::gc_cnt_t>(arms.size());
                std::vector<std::byte> bytes(sizeof(joinBody));
                std::memcpy(bytes.data(), &joinBody, sizeof(joinBody));
                const camel::runtime::DraftNodeHeader *header = draft.header(join);
                camel::runtime::DraftNodeInit init;
                init.dataIndex    = header->dataIndex;
                init.dataType     = header->dataType;
                init.kind         = header->kind;
                init.runtimeFlags = header->runtimeFlags;
                init.origin       = header->origin;
                init.permissions  = header->permissions;
                init.attrs        = header->attrs;
                init.region       = header->region;
                init.name         = header->name;
                init.payload      = std::span<const std::byte>(bytes);
                const gc_node_ref_t joinBranch = brch;
                init.normInputs   = std::span<const gc_node_ref_t>(&joinBranch, 1);
                init.withInputs   = armCotangents;
                draft.rewriteNode(join, init);
                // The arms answer one cotangent each, and the join is theirs to merge. The transposed
                // function's answer is one cotangent per parameter the transposition answers for, so
                // the join's single value is wrapped in the one-slot tuple the signature promises --
                // the shape every arity has (author, 2026-11-02 11:30).
                result = tupleOf({join}, {cotangentTypeOf(join, Type::Float64())}).first;
                break;
            }
            case rt::GCNodeKind::Oper: {
                // Where this rule's cotangent could go. A value whose type has no tangent space
                // cannot receive one, and an output with none has no cotangent to hand out -- so a
                // conversion whose only operand is an integer (`:op/ltod`, `float(n)`) stops here.
                // A rule for such an operator could only be a derivative invented for a value that
                // has none, so the cotangent is dropped and the value that stopped it is named, the
                // way an unread parameter answers zero instead of a refusal.
                {
                    if (!hasTangentSpace(ref)) {
                        noteZero(ref, ref);
                        break;
                    }
                    bool anyReceiver = false;
                    for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                        if (!isConstant(operand) && hasTangentSpace(operand)) {
                            anyReceiver = true;
                            break;
                        }
                    }
                    if (!anyReceiver) {
                        for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                            if (!isConstant(operand)) {
                                noteZero(operand, ref);
                            }
                        }
                        break;
                    }
                }
                const auto *body           = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const std::string_view uri = body->uri();
                const auto rule            = camel::core::transposeRuleOf(std::string(uri));
                if (!rule.has_value() || rule->status != camel::parse::ast::RuleLookup::Found) {
                    const char *reason =
                        !rule.has_value() ? "unbound"
                        : rule->status == camel::parse::ast::RuleLookup::NoSuchFamily
                            ? "no-such-family"
                            : "no-rule";
                    os << "TRANSPOSE function=" << function->name() << " skip=rule uri=" << uri
                       << " reason=" << reason << '\n';
                    return false;
                }
                const auto inputs = function->normInputsOf(ref);
                if (inputs.size() != 2 && inputs.size() != 1) {
                    os << "TRANSPOSE function=" << function->name() << " skip=arity uri=" << uri
                       << " operands=" << inputs.size() << '\n';
                    return false;
                }
                const gc_node_ref_t ct = cotangent[ref];
                noteRule(rule->rule);
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (isConstant(inputs[i]) || !wantsCotangent(inputs[i])) {
                        // A value parameter of a partial transposition takes no cotangent, and
                        // computing one would need the value of a participating parameter -- the
                        // one thing this graph does not have.
                        continue;
                    }
                    if (!hasTangentSpace(inputs[i])) {
                        // An operand whose type has no tangent space is not a position a cotangent
                        // goes to: there is nothing for one to be. The rule's right side is written
                        // for the positions that answer one -- `xs[i]`'s right side answers `xs`'s
                        // cotangent alone, because the `i` is an `int` -- so asking it for this
                        // position is not a gap in the rule but a question with no answer, and it is
                        // recorded the way a cotangent landing on such a value always is rather than
                        // refused. Same query as `transposeSignature`: the type answers.
                        noteZero(inputs[i], ref);
                        continue;
                    }
                    std::string why;
                    const gc_node_ref_t answer = ruleCotangent(rule->rule, inputs, i, ct, why);
                    if (answer == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name()
                           << " skip=cotangent-needs-a-value uri=" << uri << " position=" << i
                           << " reason=" << why << '\n';
                        return false;
                    }
                    addCotangent(inputs[i], answer);
                }
                break;
            }
            case rt::GCNodeKind::Loop: {
                // A loop is rounds of a region, and the state it carries is the port the loop
                // hands its own body -- so the k-th round's value has no name in the graph, and
                // the round count is a value (the loop's first norm input), not a constant. The
                // transpose therefore runs a loop of the same rounds: what the reverse needs is
                // the same chain, applied from the far end. That is enough when the body's rules
                // only use constants -- the product of the round's multipliers does not depend on
                // the order -- and it is not enough when a rule needs the round's own state, which
                // is refused by name rather than answered with a cotangent from nowhere.
                const gc_node_ref_t ctLoop = cotangent[ref];
                // The origin a refusal names is the region's: it is where the round was written.
                const uint32_t regionOrigin = static_cast<uint32_t>(function->region(function->nodeRegion(ref))->origin);
                const gc_node_ref_t bodyRegionOld = [&]() -> uint32_t {
                    for (uint32_t r = 1; r < function->regionCount(); ++r) {
                        const rt::GCRegion *part = function->region(r);
                        if (part != nullptr && part->owner == ref &&
                            part->kind == rt::GCRegionKind::LoopBody) {
                            return r;
                        }
                    }
                    return 0;
                }();
                const auto regionPorts = bodyRegionOld != 0
                    ? function->regionPorts(bodyRegionOld)
                    : std::span<const gc_node_ref_t>{};
                if (bodyRegionOld == 0 || regionPorts.empty()) {
                    os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                       << " origin=" << regionOrigin << '\n';
                    return false;
                }
                const gc_node_ref_t oldBodyResult = function->region(bodyRegionOld)->result;
                // How many states the loop already carried: the body's ports are the element
                // followed by one per state, and the loop's value is the tuple of last states. When
                // there is one, its cotangent is a float; when there are more, it is a tuple and the
                // cotangent arrives slot by slot from the component reads.
                const size_t stateCount = regionPorts.size() - 1;
                // The state's initial values are the loop's norm inputs after the range; the
                // escapes are compared with them below, so they are read once here.
                const auto oldNorm = function->normInputsOf(ref);
                const uint32_t newRegion =
                    draft.addRegion(rt::GCRegionKind::LoopBody, 0);
                draft.setInsertRegion(newRegion);
                // The body takes the iterated element first and the states after it. The element is
                // not differentiated (it comes from the range), so it keeps its type and goes
                // unused; each state becomes one cotangent, so the region grows one port per state
                // and, later, one per escaped value.
                std::vector<gc_node_ref_t> newPorts;
                const gc_node_ref_t elementPort = draft.addPortNode(
                    function->node(regionPorts.front())->dataType(),
                    draft.allocateRuntimeSlot(function->node(regionPorts.front())->dataType()));
                newPorts.push_back(elementPort);
                // A state's cotangent port lives in the state's own tangent space: a loop that
                // carries a `float[]` state pulls back an array, and a port typed `float` would hand
                // that array to a number -- the same read that took the process down when two
                // elements of one array were summed (design principles 9.9).
                std::vector<gc_node_ref_t> ctPorts;
                std::vector<Type *> stateCotangentTypes(stateCount, Type::Float64());
                for (size_t i = 0; i < stateCount; ++i) {
                    const size_t portIndex = 1 + i;
                    Type *const type       = portIndex < regionPorts.size()
                                                 ? cotangentTypeOf(regionPorts[portIndex],
                                                                   Type::Float64())
                                                 : Type::Float64();
                    stateCotangentTypes[i] = type;
                    ctPorts.push_back(draft.addPortNode(type, draft.allocateRuntimeSlot(type)));
                    newPorts.push_back(ctPorts.back());
                }
                draft.setRegionPorts(newRegion, std::span<const gc_node_ref_t>(newPorts));
                escapes.clear();
                allowEscapes = true;
                // One seed per state: the cotangent of that state's next value. For one state that
                // is the body's whole result; for several it is the component of the result that
                // the state is, which is why the components are read out here rather than the
                // tuple node being walked as one value.
                std::vector<ArmSeed> seeds;
                if (stateCount == 1) {
                    seeds.push_back(ArmSeed{oldBodyResult, ctPorts.front()});
                } else {
                    const auto results = function->withInputsOf(oldBodyResult);
                    for (size_t i = 0; i < stateCount; ++i) {
                        seeds.push_back(ArmSeed{
                            i < results.size() ? results[i] : rt::kInvalidNodeRef, ctPorts[i]});
                    }
                }
                std::vector<gc_node_ref_t> statePorts(
                    regionPorts.begin() + 1, regionPorts.end());
                const std::vector<gc_node_ref_t> back =
                    runSeeded(bodyRegionOld, seeds, statePorts);
                const gc_node_ref_t bodyCotangent =
                    back.empty() ? rt::kInvalidNodeRef : back.front();
                allowEscapes = false;
                // The escapes in target order: the accumulators, the components and the reading all
                // follow the graph rather than the map's iteration order.
                std::vector<std::pair<gc_node_ref_t, Escape>> caught(escapes.begin(), escapes.end());
                std::sort(
                    caught.begin(), caught.end(),
                    [](const auto &left, const auto &right) { return left.first < right.first; });
                // The value each state starts from: its cotangent is the state's own, and it is also a
                // value the body may have read -- an escape can land on one of them.
                std::vector<gc_node_ref_t> stateInitials;
                for (size_t i = 0; i < stateCount; ++i) {
                    const size_t at = 1 + i;
                    stateInitials.push_back(at < oldNorm.size() ? oldNorm[at] : rt::kInvalidNodeRef);
                }
                const auto isStateInitial = [&](gc_node_ref_t what) {
                    return std::find(stateInitials.begin(), stateInitials.end(), what) !=
                           stateInitials.end();
                };
                // One accumulator per escaped value, beside one cotangent per state: the loop stays one
                // loop -- not one loop per escape -- and grows a state for each value that escaped,
                // because each of them has a cotangent of its own to hand out and all of them
                // advance by the same rounds.
                const bool carries = !caught.empty();
                // Two different readings hide behind a count. One value read by several products is
                // one sum -- `answer=summed` -- and a single escape onto a value the loop's state
                // starts from is the same thing, since that state's cotangent and the sum are
                // cotangents of one value then. Several escaped values are `answer=per-target`: one
                // cotangent each, and only the ones that are a state's own value also take that
                // state's cotangent.
                const bool answerSummed =
                    caught.size() == 1 && isStateInitial(caught.front().first);
                if (armsOk && bodyCotangent != rt::kInvalidNodeRef && !caught.empty()) {
                    // What escaped, read the same way whether or not it can be handed back: the
                    // count, and one line per escape -- which value the round only read (`target`),
                    // which of the body's products handed the cotangent out (`from`, the node the
                    // rule ran on), and the loop's own two input lists, norm and with, which are
                    // what the accumulators are added to.
                    // `skip=state` said the loop refused because its one state had nowhere to put
                    // what escaped. When the channel is built nothing is skipped, so the same line
                    // says how many escapes the loop now carries and keeps every other field: the
                    // reading survives the fix, which is what makes a refusal and an answer
                    // comparable. The `norm` and `with` lists in the ESCAPE lines stay the loop's
                    // own -- they are what the states are added to, not what the escape was.
                    if (carries) {
                        os << "TRANSPOSE function=" << function->name() << " carried="
                           << caught.size() << " node=" << ref << " origin=" << regionOrigin
                           << " captures=" << caught.size()
                           << " answer=" << (answerSummed ? "summed" : "per-target") << '\n';
                    } else {
                        // The refusal's own line, character for character: the A/B of this change
                        // reads it as it stood before the accumulators existed.
                        os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                           << " origin=" << regionOrigin << " captures=" << caught.size() << '\n';
                    }
                    for (const auto &entry : caught) {
                        const camel::runtime::GCNode *carrier = function->node(entry.first);
                        const camel::runtime::GCNode *source  = function->node(entry.second.from);
                        os << "ESCAPE target=" << entry.first << " from=" << entry.second.from
                           << " kindOfFrom="
                           << (source != nullptr ? static_cast<int>(source->kind) : -1)
                           << " kindOfTarget="
                           << (carrier != nullptr ? static_cast<int>(carrier->kind) : -1)
                           << " typeOfTarget="
                           << (carrier != nullptr && carrier->dataType() != nullptr
                                   ? carrier->dataType()->toString()
                                   : std::string("-"))
                           << " norm=[";
                        for (const gc_node_ref_t input : function->normInputsOf(ref)) {
                            os << input << ',';
                        }
                        os << "] with=[";
                        for (const gc_node_ref_t input : function->withInputsOf(ref)) {
                            os << input << ',';
                        }
                        os << "]\n";
                    }
                    if (!carries) {
                        return false;
                    }
                }
                if (!armsOk || bodyCotangent == rt::kInvalidNodeRef) {
                    os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                       << " origin=" << regionOrigin << '\n';
                    return false;
                }
                // The region hands back one cotangent per state, then one sum per escaped value. With one
                // state and no escape that is a single value, which is what the region returned
                // before there were accumulators at all; with more, it is the tuple the loop carries.
                // The port list is written again rather than kept from above, because how many
                // states there are is known only after the body ran -- the same way a branch's arm
                // grows its ports when its own walk learns what it reads.
                std::vector<gc_node_ref_t> regionParts;
                std::vector<Type *> componentTypes;
                for (size_t i = 0; i < stateCount; ++i) {
                    // A state no rule reached pulls nothing back: the zero of the state's own space
                    // says so, rather than a hole in the tuple -- `0.0` for a number, an array of
                    // zeros for an array.
                    const gc_node_ref_t part =
                        i < back.size() ? back[i] : rt::kInvalidNodeRef;
                    regionParts.push_back(
                        part == rt::kInvalidNodeRef ? zeroOf(regionPorts[1 + i]) : part);
                    componentTypes.push_back(stateCotangentTypes[i]);
                }
                for (const auto &entry : caught) {
                    // The accumulator is the escaped value's own cotangent, so its port is typed by
                    // that value's tangent space -- the same answer the state ports above ask. A
                    // `float` port holding an array was this site's lie: `:op/add_d` on an array
                    // reads it as a number.
                    Type *const accType = cotangentTypeOf(entry.first, Type::Float64());
                    const gc_node_ref_t accPort =
                        draft.addPortNode(accType, draft.allocateRuntimeSlot(accType));
                    newPorts.push_back(accPort);
                    // The round adds what escaped to the sum the round before handed on. One
                    // accumulator per value, not per product: the products that call one value
                    // add into the same sum when they are delivered (the collection above). The sum
                    // is the type's own: `:tangent/add_arr` for an array, `:op/add_d` for a number.
                    regionParts.push_back(accumulate(accPort, entry.second.cotangent));
                    componentTypes.push_back(accType);
                }
                gc_node_ref_t regionResult = regionParts.front();
                Type *loopType             = componentTypes.front();
                if (regionParts.size() > 1) {
                    draft.setRegionPorts(newRegion, std::span<const gc_node_ref_t>(newPorts));
                    const auto pair = tupleOf(regionParts, componentTypes);
                    regionResult    = pair.first;
                    loopType        = pair.second;
                }
                draft.setRegionResult(newRegion, regionResult);
                draft.setInsertRegion(0);
                const gc_node_ref_t loopNode = draft.addLoopNode(
                    rt::loopBodyOf(*function, ref), loopType);
                draft.setRegionOwner(newRegion, loopNode, function->region(bodyRegionOld)->index);
                draft.setNodeOrigin(loopNode, static_cast<uint32_t>(function->region(bodyRegionOld)->origin));
                std::vector<gc_node_ref_t> loopNorm;
                // The range, and everything before the states in the old norm list: those are
                // copied as they stand, because the reverse loop iterates over the same values.
                // What follows them is the states, and those become cotangents.
                for (size_t i = 0; i + stateCount < oldNorm.size(); ++i) {
                    const gc_node_ref_t copied = forward(oldNorm[i]);
                    if (!complete || copied == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=range node="
                           << ref << '\n';
                        return false;
                    }
                    loopNorm.push_back(copied);
                }
                // One seed per state: the cotangent the loop's value carried for that state. With
                // one state that is the whole cotangent of the loop's value; with several it is the
                // slot of the tuple that the component reads filled in.
                for (size_t i = 0; i < stateCount; ++i) {
                    if (stateCount == 1) {
                        loopNorm.push_back(ctLoop);
                        continue;
                    }
                    const auto slots   = tupleCotangents.find(ref);
                    const gc_node_ref_t slot = slots != tupleCotangents.end() && i < slots->second.size()
                                                   ? slots->second[i]
                                                   : rt::kInvalidNodeRef;
                    // A slot nobody read is zero: that state's value pulls nothing back into it.
                    loopNorm.push_back(slot == rt::kInvalidNodeRef ? zero() : slot);
                }
                for (size_t i = 0; i < caught.size(); ++i) {
                    // Every sum starts at zero: a value a round never reaches adds nothing.
                    loopNorm.push_back(zero());
                }
                draft.setNormInputs(loopNode, std::span<const gc_node_ref_t>(loopNorm));
                // The With inputs of a loop are the results of its parts and its round: the round is
                // a value of its own, and the body's state port is what the previous round left.
                const gc_node_ref_t roundValue = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(0), Type::Int64(),
                    rt::kGCNodeFlagConstant);
                const std::array<gc_node_ref_t, 2> loopWith{regionResult, roundValue};
                draft.setWithInputs(loopNode, std::span<const gc_node_ref_t>(loopWith));
                if (carries || stateCount > 1) {
                    // The loop's value is one cotangent per state followed by one sum per escaped
                    // value, so each component goes where its own value's cotangent goes: a state's
                    // cotangent to the value that state starts from, a sum to the value that
                    // escaped. When those are the same value the two add, because both are
                    // cotangents of it -- that is `answer=summed`. Different values get different
                    // slots -- `answer=per-target` -- and nothing is added across values.
                    const auto component = [&](uint32_t index) {
                        // The component read is typed like the member it reads: a sum over an array
                        // is an array, and reading it as a float is the same read `add_d` made.
                        Type *const componentType = index < componentTypes.size()
                                                        ? componentTypes[index]
                                                        : Type::Float64();
                        const gc_node_ref_t part = draft.addAccsNode(componentType, index);
                        draft.setNormInputs(part, std::span<const gc_node_ref_t>(&loopNode, 1));
                        return part;
                    };
                    for (size_t i = 0; i < stateCount; ++i) {
                        if (stateInitials[i] != rt::kInvalidNodeRef) {
                            addCotangent(stateInitials[i], component(static_cast<uint32_t>(i)));
                        }
                    }
                    for (size_t i = 0; i < caught.size(); ++i) {
                        addCotangent(
                            caught[i].first, component(static_cast<uint32_t>(stateCount + i)));
                    }
                } else {
                    // One state and nothing escaped: the loop's value is that state's cotangent, and it
                    // is the transposed function's whole answer -- one cotangent, in the one-slot tuple
                    // the signature promises, the same shape a branch's answer takes (author,
                    // 2026-11-02 11:30). Handing the scalar out unwrapped is what took the process down
                    // when the graph was called: the call site's slot is typed `(float,)` by the
                    // signature, so a `float` written into it is read as a tuple.
                    result = tupleOf({loopNode}, {cotangentTypeOf(loopNode, Type::Float64())}).first;
                }
                break;
            }
            case rt::GCNodeKind::Call:
            case rt::GCNodeKind::Func: {
                // A call reaches its callee one of two ways in this graph model: a CALL node whose
                // first input is the callee's value, or a FUNC node that is the call itself, with
                // the arguments as its norm inputs. Both are one fact -- a graph is called here --
                // and both are transposed the same way: the callee's transpose is a graph of its
                // own, built beside this one, once per callee (the rule `jvp` reads for a lifted
                // function, so two calls of one function call one graph). The call site passes
                // what the transposed callee takes, its own parameters as they stand -- they are
                // the values the call already has -- and then the cotangent.
                const bool isCall = node->kind == rt::GCNodeKind::Call;
                const auto inputs = function->normInputsOf(ref);
                // Where the callee arrives from. A function value written in place is the call's
                // first normal input; a value kept in a `let` is a DATA node naming its graph and
                // the call takes it as a `with` input, with only the arguments as norm inputs; a
                // FUNC node is the call itself. All three name a graph, so all three transpose the
                // same way.
                GraphId calleeId  = function->calleeIdOf(inputs.empty() ? ref : inputs.front());
                size_t argumentAt = (!inputs.empty() && rt::isValid(calleeId)) ? 1 : 0;
                // What the one entry that names a *value* answered, when it was asked. Empty means
                // `calleeIdOf` answered and no value had to be named.
                std::string_view namedReason;
                if (!rt::isValid(calleeId)) {
                    const auto captured = function->withInputsOf(ref);
                    if (!captured.empty()) {
                        calleeId   = function->calleeIdOf(captured.front());
                        argumentAt = 0;
                        if (!rt::isValid(calleeId)) {
                            // `calleeIdOf` answers for a FUNC node -- a call written by name. A
                            // function value kept in a `let` is a DATA node holding a static
                            // function, and `staticFunctionGraphOf` is the one entry that turns that
                            // value into a graph id, or says which check refused.
                            const auto named = function->staticFunctionGraphOf(captured.front());
                            calleeId          = named.graph;
                            namedReason       = named.reason;
                        }
                    }
                }
                if (!rt::isValid(calleeId) && !isCall) {
                    calleeId   = function->calleeIdOf(ref);
                    argumentAt = 0;
                }
                if (inputs.size() <= argumentAt && !rt::isValid(calleeId)) {
                    os << "TRANSPOSE function=" << function->name() << " skip=kind node=" << ref
                       << " kind=" << static_cast<int>(node->kind) << '\n';
                    return false;
                }

                camel::runtime::GCGraph *callee = state.program->graph(calleeId);
                if (callee == nullptr) {
                    // The callee is the value the first input carries, and a value that is
                    // itself computed -- a call's result, a returned closure -- names no graph
                    // here. The refusal names the node that carries that value and its kind, so a
                    // reader can see what stands in the way without a dump.
                    const auto describe = [&](gc_node_ref_t value) {
                        const camel::runtime::GCNode *carrier = function->node(value);
                        return std::to_string(value) + ':' +
                               std::to_string(carrier != nullptr
                                                  ? static_cast<int>(carrier->kind)
                                                  : -1);
                    };
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " callee=none value=" << describe(inputs.front());
                    const auto captured = function->withInputsOf(ref);
                    if (!captured.empty()) {
                        os << " with=" << describe(captured.front());
                    }
                    if (!namedReason.empty()) {
                        os << " reason=" << namedReason;
                    }
                    os << '\n';
                    return false;
                }
                GraphId transposed = rt::kInvalidGraphId;
                // The member is built below, once the participating set is known: a callee whose
                // trailing arguments are tangents is transposed as the linear map it is, and its
                // member answers only for those. Building it here would build the every-parameter
                // form and then call it with the tangents' arguments missing.
                // The member takes the callee's *non-answering* parameters as values and then the
                // cotangent, and answers one cotangent for each parameter it answers for. Which those
                // are is `transposeSignature`'s answer, read here rather than counted a second time:
                // for `xs[i]` the member reads the `int` index and answers the array's cotangent
                // alone -- the array is a linear input, not a value -- so its answer is a one-long
                // tuple of arrays and not `(float, float)`, and the one value the call hands it is
                // the index.
                const size_t calleeParams = callee->funcType()->normTypesCount();
                if (calleeParams == 0 || inputs.size() != argumentAt + calleeParams) {
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " callee=" << callee->name() << " params=" << calleeParams << '\n';
                    return false;
                }
                std::vector<size_t> answersOf;
                // A **rule kernel** call is linear in its tangents, and which arguments those are is
                // the rule's own fact, recorded on this node when `reinterp` expanded it
                // (`GCNode::tangentArgs`): the values are the captures the rule bound, the tangents are
                // the lambda's parameters. The arm path has read that count since it existed; the
                // straight-line body reads it here for the same reason -- a closed half's body is
                // nothing but such calls, and a tangent has no value to forward.
                const uint16_t tangentArgs = function->callTangentArgsOf(ref);
                std::vector<bool> participates;
                if (tangentArgs > 0) {
                    if (tangentArgs > calleeParams) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " callee=" << callee->name() << " tangent-args=" << tangentArgs
                           << " params=" << calleeParams << '\n';
                        return false;
                    }
                    participates.assign(calleeParams, false);
                    for (size_t i = calleeParams - tangentArgs; i < calleeParams; ++i) {
                        participates[i] = true;
                    }
                }
                // The member, built as the shape the participating set names -- the same build the arm
                // path asks for, one graph per callee. It is built **before** the signature is
                // asked for, because the build is what discovers the values the member needs
                // (`TransposeState::calleeNeeds`, from the probe the build runs first) and those
                // values are capture ports of the signature: a type written before the discovery
                // would name a member that is one argument short.
                {
                    const auto known = state.callees.find(rt::toValue(calleeId));
                    if (known == state.callees.end()) {
                        transposed = group.reserve(
                            "transpose_of_" + std::string(callee->name()),
                            "transpose_of_" + std::string(callee->name()));
                        state.callees.emplace(rt::toValue(calleeId), transposed);
                        if (!buildTransposeMember(state, callee, transposed, os, participates)) {
                            os << "TRANSPOSE function=" << function->name()
                               << " skip=call node=" << ref << " callee=" << callee->name() << '\n';
                            return false;
                        }
                    } else {
                        transposed = known->second;
                    }
                }
                FunctionType *calleeType = transposeSignature(
                    callee->funcType(), participates, &answersOf, &state.calleeNeeds[callee]);
                if (calleeType == nullptr || calleeType->normTypesCount() == 0) {
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " callee=" << callee->name() << " signature\n";
                    return false;
                }
                // The member's value parameters are the callee's parameters it answers nothing for, in
                // the order they stand, so the call hands one forwarded value per such parameter.
                // `xs[i]` is why this is read from the same predicate the member's signature was
                // built from: one value per *leading* parameter would hand it the array where the
                // index belongs, and `(xs, i)` is the shape where an answered parameter stands before
                // a value.
                const size_t memberValues = calleeType->normTypesCount() - 1;
                std::vector<gc_node_ref_t> arguments;
                for (size_t i = 0; i < calleeParams; ++i) {
                    bool answered = false;
                    if (participates.empty()) {
                        const camel::core::TangentSpaceAnswer space =
                            camel::core::tangentSpaceOf(callee->funcType()->normTypeAt(i));
                        answered = space.hasSpace() && space.space != nullptr;
                    } else {
                        answered = participates[i];
                    }
                    if (answered) {
                        continue;
                    }
                    if (argumentAt + i >= inputs.size()) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " arg=" << i << " callee=" << callee->name() << '\n';
                        return false;
                    }
                    const gc_node_ref_t argument = forward(inputs[argumentAt + i]);
                    if (argument == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " arg=" << inputs[argumentAt + i] << '\n';
                        return false;
                    }
                    arguments.push_back(argument);
                }
                // The values the member needs and its own parameters cannot give it: the call site
                // forwards them from the arguments it stands on, one per index of the set, in the
                // set's order -- which is the order the member's capture ports stand in. A value
                // that cannot be forwarded refuses the whole call: a capture port left unfilled is
                // a member called with a hole in its list, which is worse than a refusal.
                for (const size_t idx : state.calleeNeeds[callee]) {
                    if (argumentAt + idx >= inputs.size()) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " arg=" << idx << " callee=" << callee->name() << '\n';
                        return false;
                    }
                    const gc_node_ref_t captured = forward(inputs[argumentAt + idx]);
                    if (captured == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " arg=" << inputs[argumentAt + idx] << '\n';
                        return false;
                    }
                    arguments.push_back(captured);
                }
                if (arguments.size() != memberValues) {
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " values=" << arguments.size() << " member=" << memberValues << '\n';
                    return false;
                }
                // The callee's transpose stands where the call stood: it takes the arguments as the
                // call has them and then the cotangent, and answers one cotangent for each argument
                // -- a number for one parameter, one tuple for several.
                // The answer is the signature's, not a second computation of it: `calleeType` is what
                // the transposed member answers, and building it again from the parameter count is how
                // a call site ends up saying `(float, float)` about a member that answers `(float[])`.
                Type *const answerType = calleeType->exitType();
                std::vector<gc_node_ref_t> callArgs;
                callArgs.reserve(arguments.size() + 2);
                // Where the callee stood at the call site, it stands in the transposed call: as the
                // first normal input when the original wrote it in place, as a `with` input when the
                // original took it as a value (`let h = double; h(x)`). A call whose callee value is
                // put back in the wrong place is a CALL node without a callee.
                gc_node_ref_t calleeValue = rt::kInvalidNodeRef;
                if (isCall) {
                    if (argumentAt == 1) {
                        // Written in place: a FUNC node, whose type is the call's value.
                        calleeValue = draft.addFuncNode(transposed, Type::Float64());
                        callArgs.push_back(calleeValue);
                    } else {
                        // Taken as a value: it has to be a function value, a static function of the
                        // transposed graph with an empty closure (nothing captured). A call whose
                        // callee *captures* cannot be transposed here -- the transposed callee would
                        // have to take the captured values as parameters of its own -- and refusing
                        // that is the next question this shape asks, not one this step answers.
                        auto *closureType = camel::core::type::TupleType::create(
                            std::vector<Type *>{});
                        auto *function = ::Function::create(
                            state.program, transposed, closureType,
                            camel::core::mm::autoSpace());
                        calleeValue = draft.materializeStaticValue(
                            toSlot<::Function *>(function), calleeType);
                    }
                }
                callArgs.insert(callArgs.end(), arguments.begin(), arguments.end());
                callArgs.push_back(cotangent[ref]);
                const gc_node_ref_t made =
                    isCall ? draft.addCallNode(answerType, rt::GCCallBody{})
                           : draft.addFuncNode(transposed, answerType);
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
                if (calleeValue != rt::kInvalidNodeRef && argumentAt == 0) {
                    draft.setWithInputs(
                        made, std::span<const gc_node_ref_t>(&calleeValue, size_t(1)));
                }
                // The cotangent goes back where the member answers it: one slot per *answering*
                // parameter, in the member's order -- not one per parameter of the callee. The `int`
                // of `xs[i]` is a parameter the member reads and answers nothing for, so no slot of
                // the answer is its, and a cotangent handed to it would be a value invented for a
                // position that has none.
                const auto *answerTuple = answerType == nullptr
                                              ? nullptr
                                              : dynamic_cast<const camel::core::type::TupleType *>(
                                                    answerType);
                for (size_t k = 0; k < answersOf.size(); ++k) {
                    if (answersOf[k] >= calleeParams) {
                        continue;
                    }
                    if (answerTuple == nullptr) {
                        // One parameter, one answer: the cotangent is the call's value.
                        addCotangent(inputs[argumentAt + answersOf[k]], made);
                        continue;
                    }
                    const gc_node_ref_t part = draft.addAccsNode(
                        answerTuple->typeAt(k), static_cast<uint32_t>(k));
                    draft.setNormInputs(part, std::span<const gc_node_ref_t>(&made, 1));
                    addCotangent(inputs[argumentAt + answersOf[k]], part);
                }
                break;
            }
            default:
                os << "TRANSPOSE function=" << function->name() << " skip=kind node=" << ref
                   << '\n';
                return false;
            }
        }
        if (result == rt::kInvalidNodeRef) {
            // The answer is one cotangent per parameter the transposition answers for, in the order
            // the parameters stand -- a tuple for every function, a one-long one for a function of
            // one parameter, because the law states one shape for every arity (author, 2026-11-02
            // 11:30). This is the only way the answer is built: no exit is a bare cotangent, so the
            // type the signature promised and the tuple this builds cannot come apart.
            //
            // A parameter no cotangent reached is answered with a zero. A parameter the graph never
            // reads pulls nothing back -- the `x` and `w` of `add_tangent(x, w, dx, dw) = dx + dw`
            // are in the signature and nowhere in the body -- and refusing the whole function
            // because one of its parameters has no derivative would refuse a function that is
            // written correctly. The zero is the same statement the walk makes when it drops the
            // cotangent of a value: that component is read, not differentiated.
            const auto &ports      = function->normPorts();
            const size_t firstPort = bundlesResiduals ? 1 : 0;
            std::vector<gc_node_ref_t> parts;
            // Each answered parameter's cotangent, and the type it has: an array parameter's
            // cotangent is an array. The type comes from the parameter's own answer
            // (`tangentSpaceOf`, the query the signature reads), so nothing here says `float` on
            // behalf of a type that is not one. A parameter with no tangent space answers nothing
            // and has no slot, which is what keeps the tuple the signature promised and the tuple
            // this builds the same length.
            std::vector<Type *> partTypes;
            for (size_t i = firstPort; i < ports.size(); ++i) {
                if (!participatesOf(i)) {
                    // A parameter the transposition does not answer for -- a value the graph reads,
                    // or one no cotangent can land on: no member of the answer is its.
                    continue;
                }
                const camel::runtime::GCNode *portNode = function->node(ports[i]);
                const Type *portType = portNode != nullptr ? portNode->dataType() : nullptr;
                const camel::core::TangentSpaceAnswer space =
                    portType == nullptr ? camel::core::TangentSpaceAnswer{}
                                        : camel::core::tangentSpaceOf(portType);
                if (portType != nullptr && !space.hasSpace()) {
                    // Nothing can be where a cotangent goes, so this parameter answers none: the
                    // `i` of `xs[i]` is an `int`.
                    continue;
                }
                if (hasCotangent[ports[i]]) {
                    parts.push_back(cotangent[ports[i]]);
                    partTypes.push_back(space.space);
                    continue;
                }
                // The question is which space, not which object: a `float` cotangent is the one whose
                // tangent space *is* the float type, and asking by pointer would silently answer "not
                // float" for any float type that is not the interned one.
                if (portType == nullptr || space.space->code() == camel::core::type::TypeCode::Float64) {
                    parts.push_back(zero());
                    partTypes.push_back(Type::Float64());
                    continue;
                }
                if (space.space->code() == camel::core::type::TypeCode::Array) {
                    // An array parameter the backward half never read. Its cotangent is the
                    // zero of its own space, and the zero of an array's tangent space is the
                    // array of zeros built from the value's shape -- `:tangent/zeros_arr`,
                    // the operator the pullback already builds for the same purpose
                    // (`idxArrayVjp`, builtin/operators/derivative.cpp). The parameter's port
                    // carries the value the shape comes from, so the zero is built where the
                    // value already stands. A zero cotangent is the identity of the sum the
                    // pullback takes, so answering it is the same as answering nothing, in the
                    // type the slot promises; refusing the call instead stops a function that
                    // is written correctly -- providing the parameter is read by the forward
                    // half, which is why a parameter with no tangent space was already let
                    // through above.
                    const gc_node_ref_t zeros =
                        draft.addOperNode(space.space, nullptr, ":tangent/zeros_arr");
                    draft.setNormInputs(zeros, std::span<const gc_node_ref_t>(&ports[i], 1));
                    parts.push_back(zeros);
                    partTypes.push_back(space.space);
                    continue;
                }
                // A value with a cotangent space and no cotangent: the graph never read it. A zero of
                // its own space would have to be built from the value's shape, which this walk does
                // not carry for anything but a number; refuse by name rather than answer a `float`
                // where an array is promised.
                os << "TRANSPOSE function=" << function->name() << " skip=unread-parameter port="
                   << ports[i] << " type=" << (portType != nullptr ? portType->toString() : "?")
                   << '\n';
                return false;
            }
            if (!parts.empty()) {
                std::vector<size_t> slots;
                for (size_t i = 0; i < parts.size(); ++i) {
                    slots.push_back(i);
                }
                Type *const tupleType = camel::core::type::TupleType::create(partTypes);
                ::Tuple *emptyTuple    = ::Tuple::create(parts.size(), mm::autoSpace());
                const gc_node_ref_t source = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple),
                    tupleType);
                const gc_node_ref_t tuple = draft.addFillNode(
                    tupleType, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
                draft.setNormInputs(tuple, std::span<const gc_node_ref_t>(&source, 1));
                draft.setWithInputs(tuple, std::span<const gc_node_ref_t>(parts));
                result = tuple;
            }
        }
        if (result == rt::kInvalidNodeRef) {
            os << "TRANSPOSE function=" << function->name() << " skip=output\n";
            return false;
        }
        draft.setExitNode(result);
        draft.setOutputNode(result);
        draft.setReturnNode(result, rt::GCReturnKind::Self);
    }
    std::string rhs;
    for (const std::string &rule : usedRules) {
        rhs += (rhs.empty() ? "" : ",") + rule;
    }
    os << "TRANSPOSE function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << signature->toString() << " nodes=" << nodeCount
       << " arms=" << armRegionsBuilt.size() << " rhs=" << (rhs.empty() ? "-" : rhs)
       << '\n';
    return true;
}

TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return TransposeGraph{};
    }
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "transpose_of_" + std::string(function->name()),
        "transpose_of_" + std::string(function->name()));
    TransposeState state{group, program, {}, context};
    if (!buildTransposeMember(state, function, member, os)) {
        return TransposeGraph{};
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    return TransposeGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}



} // namespace camel::transforms
