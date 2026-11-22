// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The signature a transposition is written from -- `transpose/signature.h` declares the readings it
 * is built out of, and `transposeSignature` itself is the contract `transpose.h` states.
 *
 * Every answer here comes from the source function's type: which parameters have a tangent space,
 * which places a cotangent is answered for, which values the caller still supplies as captures, and
 * what extents a law is handed. Nothing reads a node's kind or a written type's name.
 */

#include "transpose/signature.h"

#include "transpose.h"

#include "transpose/cotangent.h"
#include "lift.h"

#include "camel/core/derivative.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "camel/utils/type.h"

#include <optional>
#include <string>
#include <vector>

namespace camel::transforms {

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;

namespace mm = camel::core::mm;
namespace rt = camel::runtime;

/// The type of `transpose(f)`: `(f: (..) => r) => (cotangent: r) => (..cotangents)`. It is the
/// operator's own type, and it is **built from the same law** the operator's resolver asks
/// (`transpose_operator.cpp`), so the `transpose` node a transposition of a value known at run time
/// writes is typed exactly the way a `transpose(..)` a program writes is: one normal parameter
/// named the way the declaration names it, and the transposed function as the exit.
FunctionType *transposeOperatorType(FunctionType *function) {
    FunctionType *transposed = transposeSignature(function);
    if (transposed == nullptr) {
        return nullptr;
    }
    camel::core::type::FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(transposed);
    return factory.build();
}

FunctionType *transposeSignature(
    FunctionType *function, const std::vector<bool> &participates, std::vector<size_t> *answersOf,
    const std::vector<size_t> *captures, bool liftedSignature) {
    if (answersOf != nullptr) {
        answersOf->clear();
    }
    if (function == nullptr) {
        return nullptr;
    }
    if (function->normTypesCount() == 0) {
        return nullptr;
    }
    // What the cotangent of the result is: the result's *tangent space*, asked of the type. A
    // `float` answers `float` and an `f32` answers `f32`, so this is one law for both widths rather
    // than a `float` law that a narrow float has to be read into: `Tangent<f32>` is `f32`
    // (design principles 9.9, `lib/types/types.cml#Tangent`), and the cotangent the caller passes, the
    // cotangent the answer's slots hold and the cotangent every rule is instantiated at are that
    // one type. A result whose cotangent this file has no arithmetic for is refused by
    // `transpose/cotangent.cpp#hasResultCotangent` -- it is the same question
    // `#cotangentTypeOfResultType` answers there, asked once, before a signature is built from it.
    if (!hasResultCotangent(function->exitType())) {
        return nullptr;
    }
    // The law: `transpose(g)` treats **every parameter
    // of `g` as a linear input** and answers `(y-bar) => (one cotangent per parameter)`. So no
    // parameter of `g` that takes a cotangent is a value its caller passes, and a body that only
    // reads such parameters gives a transposed function that takes the cotangent of the result and
    // nothing else. Two things stand before that cotangent instead of being passed as themselves:
    // a parameter whose type has no tangent space (an `int`, a `bool`, a string) is a value the
    // call site still fills, and so is a value the body reads from outside; which parameters those
    // are is a fact of the body, read below from the function rather than from its type.
    //
    // The declaration in `lib/types/types.cml#Transpose` is the contract: a declaration is what the builder makes, and no declaration is
    // approximate. That the two do not agree today is a missing capability of the type-bundle
    // splice
    // -- multiplicity is lost, and the exact shape needs an operation saying which parameters stand
    // before the cotangent -- so this build is the side that has to move once the splice can write
    // it, and the difference is a defect to be closed rather than a wording to be kept. A fixed
    // value a body needs is not a parameter: it is captured by the function that is transposed --
    // `transpose((dx) => f_lin(x, dx))(y-bar)` -- and that is the caller's spelling. What it buys
    // is that the type of a function says nothing about which half of a pair it came from, so no
    // bit has to be carried and the operator resolver -- which sees a type and nothing else -- has
    // every fact it needs.
    //
    // `participates` is the one exception, and it is a fact of a *call site* rather than of a type:
    // where a call records which of its arguments are tangents, those are the parameters a
    // cotangent is answered for and the rest are values the caller still passes. The record is a
    // position per argument (`GCNode::tangentArgs`), not a count of trailing ones, because a rule
    // writes a tangent where its right side writes it. That is the shape a rule's tangent function
    // has -- `mul_tangent(x, w, dx, dw)` is linear in `(dx, dw)`, so the cotangent of such a call
    // is the cotangent of its two tangents while its first two arguments remain values, and
    // `index_into(dxs, i)` is linear in its first argument while the index stays a value. Empty
    // means every parameter participates, which is the ordinary transposition of an entry.
    //
    // A parameter with no tangent space (an integer, a `bool`, a string) cannot be what a cotangent
    // is, and is read as a value: nothing enters or leaves its place in the answer. A function
    // whose parameters all lack a space has no transpose at all.
    const size_t count = function->normTypesCount();
    // **Which places are values and which are the places a cotangent is answered for**: the lift
    // law's answer, asked once (`lift.h#liftedParametersOf`), and the one reader this file uses for
    // it. A signature that is itself a `Lift` has a value behind every port and a tangent beside
    // it, so the tangent-space question cannot stand for "this place appears only as a tangent": the
    // law writes which half is which, and both halves are read from it. A signature that is not a
    // lift is read by the same entry the ordinary way -- a parameter with no cotangent is a value
    // the call site fills, one with a cotangent is a place the answer is about -- so a change to
    // what a lift is reaches this signature, the ports it writes and the answer it promises
    // together. Which of the two readings applies is `liftedSignature`: a fact about the *graph*
    // the signature belongs to, since the two shapes are one type (`lift.h` says why) -- narrowed
    // by the one reading this file adds on top of the law (`transposedPlacesOf`): a lift of a
    // function that eats only tangents is linear in every place of its list, so it is read the
    // ordinary way and its value half is not a port.
    const LiftedParameters places = transposedPlacesOf(function, liftedSignature);
    if (!participates.empty()) {
        // The participating-set form is a **call site's**: one flag per parameter of the callee,
        // saying which places a cotangent is answered for. What it requires is the same thing the
        // loop below requires of any signature, and nothing about the *types*: a place a cotangent
        // is answered for must have a tangent space (there is no type to put in the answer
        // otherwise, and the loop below refuses that place by name), and a place it is not is a
        // value the call site fills, of whatever type that value has.
        //
        // Demanding `Type::Float64()` of **every** parameter would be the record's shape, not the
        // shape's: a rule's right
        // side is linear in its tangents and its values keep their own types, so a parameter that
        // is not a number is a value and not a reason to refuse. The array element's law is the one
        // that shows it -- `index_into(xs: float[], i: int)` answers a cotangent for the array and
        // reads the index, and its member signature `(int, float[], float) => (float[],)` is that
        // shape, built and run by the type-based reading since before this form existed. Keeping
        // the `float` demand while the record named only a trailing run hid the difference, because
        // the record was empty for that call; once the record names positions it is not (`grad_array_no_int` and others would read `TRANSPOSE
        // function=rulesets.jvp::index_into<float> skip=signature`).
        if (participates.size() != count) {
            return nullptr;
        }
        for (size_t i = 0; i < count; ++i) {
            if (!participates[i]) {
                continue;
            }
            const camel::core::TangentSpaceAnswer space =
                camel::core::tangentSpaceOf(function->normTypeAt(i));
            if (!space.hasSpace() || space.space == nullptr) {
                return nullptr;
            }
        }
    }
    camel::core::type::param_vec_t params;
    std::vector<Type *> cotangentsOfParticipants;
    for (size_t i = 0; i < count; ++i) {
        Type *const parameter                       = function->normTypeAt(i);
        const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(parameter);
        const bool participatesHere =
            participates.empty() ? places.answers(i) : participates[i];
        if (!participatesHere) {
            if (participates.empty() && isUnitType(parameter)) {
                // `()` is an **answer** with an empty slot, not a parameter:
                // `(..Tangent<ParamsOf<F>>)` has one member per parameter, so `Tangent<int>` is
                // `()` and the pullback's answer keeps the place even though nothing can land in
                // it. It takes no port -- nobody can pass a value where there is none.
                // `grad_array_with_int.fvm` read `(float[],)` where the declaration said `(float[],
                // ())` for exactly this missing place. An `int` parameter is *not* this: it is a
                // value the caller still passes, so it falls through to the value parameters below
                // and answers nothing.
                cotangentsOfParticipants.push_back(
                    camel::core::type::TupleType::create(std::vector<Type *>{}));
                if (answersOf != nullptr) {
                    answersOf->push_back(i);
                }
                continue;
            }
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
        // A parameter whose cotangent is answered for normally takes no value port: the answer is
        // *about* it and the caller holds no value for it. The exception is the value half of a
        // lift, and it is not an exception here at all: the law reads those places as values
        // (`LiftedPlace::Value`), so they took their port above and this loop is looking at a
        // tangent place. A place is one of the two halves by the law and never by a special case in
        // this signature.
    }
    if (cotangentsOfParticipants.empty()) {
        return nullptr;
    }
    // The values the body asked for and the caller still has: each is a port of its own, of the
    // parameter's own type, standing before the cotangent. A parameter that already stands among
    // the value parameters above is reused, not added again -- the port is one per parameter, and
    // the call site's argument count has to match the ports it fills, not the set it was given.
    //
    // "Already stands" is the parameter's own place in the list, read from the same predicate the
    // list was built from: a parameter no cotangent is answered for is a value parameter. It is
    // *not* "some parameter of the same type stands": two `float` parameters are one type and two
    // places (`div_tangent(a, b, da, db)` has three values the caller passes for four parameters),
    // and a capture that reused another parameter's port would hand the body that parameter's
    // number and put two places where the caller fills one.
    if (captures != nullptr) {
        for (const size_t at : *captures) {
            if (at >= count) {
                return nullptr;
            }
            Type *const parameter = function->normTypeAt(at);
            const bool answeredHere =
                participates.empty() ? places.answers(at) : participates[at];
            if (answeredHere) {
                params.emplace_back(parameter, false);
            }
        }
    }
    params.emplace_back(resultCotangentType(function), false);
    // The transposed function declares the source's `with` parameters, in the same order and of the
    // same types: a body that reads a weight reads it on both sides of the transpose, and a `with`
    // value is a value the call supplies, not a cotangent it answers for. `Transpose<F>` says the
    // answer is one cotangent per ordinary parameter, so a `with` parameter has no member there --
    // which is the same statement the forward transform makes when it gives a captured value the
    // zero tangent (design principles 9.9).
    camel::core::type::param_vec_t withParams;
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        Type *withType = function->withTypeAt(i);
        if (withType == nullptr) {
            return nullptr;
        }
        if (function->withIsVarAt(i)) {
            if (Type *value = camel::core::type::valueTypeOf(withType)) {
                withType = value;
            }
        }
        withParams.emplace_back(withType, false);
    }
    return FunctionType::create(
        withParams,
        params,
        camel::core::type::TupleType::create(cotangentsOfParticipants));
}

/// A loop whose body let a cotangent escape carries that cotangent as a state of its own.
/// the loop is built with a second state -- the running sum of what escaped -- and answers the
/// escaped value with the state's own cotangent plus that sum. Before this, the loop refuses
/// exactly as it did before the channel existed, which is what the A/B of the reading below flips:
/// the same case, the same line, `skip=state` instead of `carried=1`.

/// A tuple value has a cotangent slot by slot: reading one component of a tuple fills
/// that slot and leaves the rest zero, so a value built as a tuple can be differentiated through
/// and a loop that carries several states can be transposed at all. Before this, a component read
/// drops its cotangent as a read of a known value -- which is what the transform did before this
/// channel, and what the A/B of the cases below flips back to: everything tuple-shaped refuses with
/// `skip=output`.

/// A value whose type has no tangent space is a zero of the derivative rather than a reason to
/// refuse: a cotangent that lands on such a value is dropped, and the reading says which value it
/// was and why -- an integer is a step function, constant between its jumps, so `float(int(3x))`
/// contributes nothing to the derivative of what it is added to, and `x + float(int(3x))`
/// answers 1. The type answers this (`camel/core/tangent_space.h`), not the node kind: the same
/// rule covers the booleans, the strings and a function value. Before this, an integer in a loop
/// body stops the body's walk where it stands -- which is what the transform did before this rule,
/// and what the A/B of the case below flips back to: `skip=state`, with no answer at all. (The
/// switch is gone; the rule and the reading are what remain.)

/// A call's recorded tangent-argument positions are read (`GCNode::tangentArgs`, one bit per
/// argument: which of the call's arguments a rule's expansion said are tangents). A kernel call
/// inside an arm is transposed as the linear map it is -- the values are ports, the tangents are
/// what the answer is a cotangent of, and the call site passes the values and the cotangent only.
/// Before this, the record is unread, the call is a plain function of all its parameters, and the
/// arm stops at the tangent that has no value: `arm-stop region=1 node=7 arg=1
/// callee=autodiff::mul_tangent<float>`. The A/B of the two readings.

/// A callee that arrives as a *value* is named. `calleeIdOf` answers for a FUNC node -- a call
/// written by name; a function value put into a `let` and called is a DATA node holding a static
/// function, and `GCGraph::staticFunctionGraphOf` is the one entry that turns it into a graph id
/// (or says which check refused: not a data node, no static slot, not a function type, no function
/// value, no graph, not a static reference of this graph). Before this, that shape refuses with
/// `skip=call callee=none`, which is what the transform did before this entry existed -- the A/B of
/// `transpose_function_value_no_capture.cml` flips exactly this. A value with no capture names its
/// graph and transposes; a closure names its graph too, and then fails later, on the capture. (The
/// switch that guarded this is gone; the entry it guarded is the one the cases read.)

/// The one `int[]` type an extents value has: `extent_of::<A>()` answers `int[]` and nothing else.
camel::core::type::ArrayType *intArrayTypeOf() {
    static camel::core::type::ArrayType *intArrayType =
        camel::core::type::ArrayType::create(camel::core::type::Type::Int64());
    return intArrayType;
}

/// The extents a type states, as the `int[]` value `extent_of::<A>()` answers a program -- written
/// into this graph where a law needs it and no program wrote the call.
///
/// `transposeSignature` gives the transposed function the cotangent and no parameter, so a law that
/// spreads a cotangent over a parameter's positions has the type and not the argument. What the
/// type says is the shape, and the shape is what a transposition needs: the adjoint of a linear map
/// is a function of the cotangent and of the domain's shape.
///
/// The shape is not read off any value: it is asked of the language, `extent_of::<A>()`, whose one
/// spelling is `camel::core::type::statedExtentsOf` -- the same rule the builtin's resolver answers
/// with (`src/builtin/operators/struct.cpp#extentOfResolver`), so the walk and a program that
/// writes `extent_of::<float[2, 3]>()` read one fact the same way. What this writes into the graph
/// is that answer, the `int[]` itself: a law of the reduction family (`sum_from_type_t` and its
/// neighbours) is handed the extents and builds what it needs from them, so no array stands in for
/// the operand and no element of one can be read by mistake.
///
/// Nothing when the type states no shape: then the caller refuses by name instead of inventing one.
/// The extents a type's shape states, or nothing when the type leaves one of them open.
///
/// The reading is `rule_expand.h#statedShapeOf`'s, and there is one of it: the rule layer hands the
/// same numbers to a law that asks for an operand's shape (`expandRuleRightSide`, and the sector it
/// is written for is the one below), so a second answer here would be a day when the two transforms
/// disagree about which types have a shape that can be written while the graph is built.
/// `jvp.cpp#zeroConstantFor` makes the same reading of the same fact.

/// The value a law's right side is handed for a parameter a cotangent is answered for: **nothing**.
///
/// `transposeSignature` gives the transposed function the cotangent and no parameter, so a law that
/// reads the parameter has the type and not the argument. A zero of the parameter's type must not stand in for it: what a
/// transposition needs of the domain is its **shape**, and every element of such a zero would be
/// zero, so a law that read an *element* would be answered a number that is not the argument.
/// What a law needs of the position it
/// answers is now one of two things, and neither is a value of this walk's making: a **shape**
/// answered from the port's type where the law reads it through `tensor:shape`
/// (`modules/transforms/rule_expand.cpp#answerTheShapeOf`), or the **extents handed over as
/// operands** of the node (`stdlib/rulesets/jvp.cml#jvp_linear`, `#jvp_erf`: the `*_stated` twins,
/// whose transpose laws read the lists and no `shape()` at all).
///
/// Nothing when the type leaves an extent open: then the shape is written down nowhere, and the
/// caller refuses by name instead of inventing one.

gc_node_ref_t statedExtentsNodeOf(camel::runtime::GraphDraft &draft, Type *type) {
    const std::optional<std::vector<int64_t>> extents = camel::core::type::statedExtentsOf(type);
    if (!extents.has_value()) {
        return rt::kInvalidNodeRef;
    }
    camel::core::type::ArrayType *const intArrayType = intArrayTypeOf();
    ::Array *stated = ::Array::create(mm::autoSpace(), intArrayType, extents->size());
    if (stated == nullptr) {
        return rt::kInvalidNodeRef;
    }
    for (size_t at = 0; at < extents->size(); ++at) {
        stated->set<camel::core::rtdata::Int64>(
            at,
            static_cast<camel::core::rtdata::Int64>((*extents)[at]));
    }
    return draft.materializeStaticValue(
        camel::core::rtdata::toSlot<::Array *>(stated),
        intArrayType,
        rt::kGCNodeFlagConstant);
}

/// The shape of a parameter as a value: `tensor:shape` is not needed here and this is **not** a
/// value of the parameter. What a law asks an operand for is its extents, and they arrive either as
/// the node's own `*_stated` operand or as `rule_expand.cpp#answerTheShapeOf`'s answer;
/// `shapeFromTypeOf`, which built `tensor:zeros` of the type's extents and handed it to a law that
/// read the operand, is not used, for the reason `#shapeCarrierOf`'s note gives.

/// The parameter whose **declaration** leaves `value`'s shape open, or nothing.
///
/// `value` is an operand of a step whose cotangent a law is answering for, or the capture a law's
/// right side read and was not handed. The walk goes back through the operators that produced it to
/// a port of the function; a port is a parameter through `portParameter`, and only a parameter
/// whose declared type is an array that states no extents is the repair -- a parameter that states
/// its extents has done its part, and a type that states none but belongs to a value the body
/// computed (an operator whose result type is open) is not a thing a declaration of this function
/// can fix.
///
/// One reading, two callers: the law-choice path refuses a reduction law whose extents no operand
/// states, and the rule-expansion path refuses a law that could not be handed an operand. Both are
/// the same class -- the declaration states no shape -- so both must name the same parameter, and a
/// second walk-back would be a day when they disagree about which declaration to edit.
std::optional<size_t> unstatedParameterBehind(
    const camel::runtime::GCGraph *function, gc_node_ref_t value,
    const std::vector<size_t> &portParameter, size_t paramCount, size_t nodeCount) {
    const camel::runtime::GCNode *const valueNode =
        function != nullptr ? function->node(value) : nullptr;
    const Type *const valueType = valueNode != nullptr ? valueNode->dataType() : nullptr;
    if (valueType == nullptr || valueType->code() != camel::core::type::TypeCode::Array ||
        camel::core::type::statedExtentsOf(valueType).has_value()) {
        return std::nullopt;
    }
    const auto ports = function->normPorts();
    std::vector<gc_node_ref_t> pending{value};
    std::vector<bool> seen(nodeCount, false);
    while (!pending.empty()) {
        const gc_node_ref_t at = pending.back();
        pending.pop_back();
        if (at >= nodeCount || seen[at]) {
            continue;
        }
        seen[at]                                 = true;
        const camel::runtime::GCNode *const node = function->node(at);
        if (node == nullptr) {
            continue;
        }
        if (node->kind == rt::GCNodeKind::Port) {
            for (size_t p = 0; p < ports.size(); ++p) {
                if (ports[p] != at) {
                    continue;
                }
                const size_t param = p < portParameter.size() ? portParameter[p] : paramCount;
                if (param < paramCount) {
                    const Type *const declared = function->funcType()->normTypeAt(param);
                    if (declared != nullptr &&
                        declared->code() == camel::core::type::TypeCode::Array &&
                        !camel::core::type::statedExtentsOf(declared).has_value()) {
                        return param;
                    }
                }
            }
            continue;
        }
        for (const gc_node_ref_t in : function->normInputsOf(at)) {
            pending.push_back(in);
        }
    }
    return std::nullopt;
}

} // namespace camel::transforms
