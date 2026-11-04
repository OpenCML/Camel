// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "jvp.h"

#include "refusal_readings.h"
#include "rule_expand.h"
#include "rule_types.h"

#include "camel/compile/gir/rule_templates.h"
#include "camel/core/forward.h"
#include "camel/core/derivative.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph_txn.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::compile::gir::FrozenGraphRead;
using camel::compile::gir::GraphRead;
using camel::compile::gir::NodeFace;
using camel::runtime::GraphId;

namespace {

/// The type of the lifted *pair* of a value: `(v, dv)`, written at the value's own type, because
/// the tangent of a value is a value of its own type wherever `tangentSpaceOf` answers `Self`. Null
/// when the value has no pair this transform can write.
///
/// The question is `tangentSpaceOf`'s (design principles 9.9: what a value's tangent is belongs to
/// its type), and the answer is the value's own type -- so `jvp` of an `f32` function is pairs of
/// `f32`, not of `float`. Until 2026-11-02 this was `Type::Float64()`, one member of the floating
/// family written down as if it were the family: the rule instance came out as `mul_tangent<float>`
/// around `f32` values and the pass gave up on four broken edges (the author's review, #1 and #11).
/// The same day it asked a second question as well -- *is the value a floating scalar* -- and
/// answered no for a tuple and for an array. A tuple and an array of floats are `Self` too, so that
/// second question was this transform's own policy written as if it were the type's: it kept a loop
/// whose state is an aggregate from being lifted at all (`skip=body`), a value the language takes
/// and the derivative of which `tangentSpaceOf` has already named. One question is asked here now,
/// and what a *reader* of a pair can do with an aggregate pair is the reader's question, asked
/// where the pair is read -- not a second gate at the door.
camel::core::type::TupleType *pairTypeOf(camel::core::type::Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(type);
    if (space.kind != camel::core::TangentSpaceKind::Self || space.space != type) {
        return nullptr;
    }
    return camel::core::type::TupleType::create(
        std::vector<camel::core::type::Type *>{type, type});
}

/// The empty tuple `()`, which is the tangent space of a parameter that has none
/// (`Tangent<int> = ()`, `stdlib/types.cml`). It is **the** empty tuple type and not a second one:
/// `TupleType::create()` (`src/core/type/composite/tuple.cpp#TupleType::create`) is the
/// zero-argument form of the interning constructor, so every place that writes `()` -- this one,
/// `split`'s empty closure, `vmap`, `typecall` -- gets the same object back. A helper rather than a
/// bare call so that the reason a signature has an empty slot at all is written where the slot is
/// made.
camel::core::type::Type *emptyTupleType() {
    return camel::core::type::TupleType::create();
}

/// Whether the tangent the lift is about to hand a rule is a **structurally** zero one: the zero the
/// lift itself wrote for a value that cannot move (`zeroMade`), or the constant zero of the
/// tangent's own space, written in this graph -- `d(float(n))` for a constant `int` `n` is `0.0`, a
/// literal of that law, so an exponent spelled `2` rather than `2.0` has a zero tangent too.
///
/// **Which types have such a constant is the tangent space's answer** (`zeroTangentConstantOf`,
/// `camel/core/derivative.h`), not a list written here: a tangent space states its zero as a
/// constant when the zero is one value -- every float width the registry knows, `f64` and `f32` as
/// well as `bf16` and `f16` -- and as an operator when it is made from a value (an array's
/// `:tangent/zeros_arr`, which is a zero `zeroMade` holds and this function does not have to
/// recognise by value). Naming only the widths that happened to be written down here is what made
/// this question half a question: a `bf16` or `f16` constant's zero is the same fact as a `float`
/// one's, and it fell through to `false` (design principles 9.9: the tangent space is settled type
/// by type, so a type the registry answers for is not this transform's to re-list).
///
/// A value a *program* computed is not one, even when it happens to be `0.0`. The distinction is the
/// author's ruling of 2026-11-04: a structural zero is known before anything runs, because a
/// constant has no tangent, and that is what lets the rule expansion drop the term it multiplies
/// (`rule_expand.cpp#expandRuleRightSide`) instead of computing it and multiplying by zero.
bool isStructuralZeroTangent(
    camel::runtime::GraphDraft &draft, const std::unordered_set<gc_node_ref_t> &zeroMade,
    gc_node_ref_t ref) {
    if (ref == rt::kInvalidNodeRef) {
        return false;
    }
    if (zeroMade.count(ref) != 0) {
        return true;
    }
    const rt::DraftNode *node = draft.node(ref);
    if (node == nullptr || node->header.kind != rt::GCNodeKind::Data ||
        node->header.dataIndex >= 0 || node->header.dataType == nullptr) {
        return false;
    }
    const std::optional<slot_t> zero = camel::core::zeroTangentConstantOf(node->header.dataType);
    if (!zero.has_value()) {
        return false;
    }
    const std::span<const slot_t> slots = draft.staticSlots();
    const auto rawIndex                 = -static_cast<std::ptrdiff_t>(node->header.dataIndex);
    const size_t index                  = static_cast<size_t>(rawIndex);
    if (rawIndex < 0 || index >= slots.size()) {
        return false;
    }
    // The constant *is* the zero its space registers, bit for bit: `-0.0` is a different constant
    // from the `0.0` the registry states, and nothing in this transform writes one (a lift writes
    // `zeroTangentConstantOf`, a law writes its own literal).
    return slots[index] == *zero;
}

/// The tangent of one primitive: the rule `stdlib/rulesets/jvp.cml` files for its operator, applied
/// to the operands' values and their tangents.
///
/// The rule is read from the family's index every time it is asked for (camel/core/forward.h):
/// remove `jvp_div` from the ruleset and the next question has no answer, which is what a transform
/// must see. There is no table here -- a second writing of the four laws in C++ was the thing this
/// kernel started with and the thing that had to go, because two tables that can disagree is worse
/// than one that is missing.
///
/// A rule is a rewrite, and its right side is a function of the tangents. `reinterp` applies it
/// *at the match site*: the one lambda layer is expanded here, so what stands where the operator
/// stood is a direct call to the named rule function -- `mul_tangent(x, w, dx, dw)`, with the
/// operands the rule captured as the first ports and the tangents after them. The alternative --
/// calling the right side and calling what it answers -- is what a rule rewritten as a *runtime*
/// call produces: a closure-valued node and a call that answers with a closure, neither of which
/// belongs in a graph the transform builds at compile time.
///
/// Whether a tangent is **structurally** zero -- the other question a lift asks before it chooses a
/// rule -- is `isStructuralZeroTangent` above: the rule expansion is handed that answer, and a term
/// whose tangent has a structural zero is omitted there rather than computed and multiplied by zero.

gc_node_ref_t buildRuleTangent(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction,
    std::string_view uri, const std::vector<gc_node_ref_t> &values,
    const std::vector<gc_node_ref_t> &tangents, camel::core::type::Type *elementType,
    const std::vector<camel::core::type::Type *> &portTypes, size_t dataOperandCount,
    std::string &ruleName, std::string &why,
    const std::unordered_set<gc_node_ref_t> *structurallyZero = nullptr,
    std::vector<std::string> *omitted = nullptr) {
    // The candidates, in the order the family filed them. A key can hold several rules -- one
    // spelling names several operators (`float(x: float)` and `float(x: int)` are both `float` at
    // one operand) -- and the type decides: the instance is asked for at the type the *operand* is
    // made of (`rule_types.h` answers that question for both transforms; writing
    // `Type::Float64()` here was the review's P1-9), and a rule whose condition does not hold there
    // is skipped rather than applied. The last refusal is reported, so a caller that tried
    // everything says the closest thing to why.
    const auto candidates = camel::core::forwardRulesOf(std::string(uri));
    if (candidates.empty()) {
        const auto match = camel::core::forwardRuleOf(std::string(uri));
        if (!match.has_value()) {
            why = "the operator has no forward rule bound at all";
        } else if (match->status == camel::parse::ast::RuleLookup::NoSuchFamily) {
            why = "no ruleset module is loaded (import rulesets.jvp)";
        } else {
            why = "the ruleset holds no rule for this operator and operand count";
        }
        return rt::kInvalidNodeRef;
    }
    // Which of the key's laws answers these operands is one question, asked in one place
    // (`rule_expand.cpp#chooseRuleAmong`); `jvp` is one of the three families that ask it, and the
    // words a refusal is written with are the only part that is this family's own.
    const RuleChoice choice = chooseRuleAmong(
        context, candidates, std::vector<camel::core::type::Type *>{elementType}, portTypes,
        dataOperandCount, RuleChoiceWords{"rule", "arguments"});
    if (!choice.found()) {
        why = choice.why;
        return rt::kInvalidNodeRef;
    }
    ruleName = choice.rule;
    return expandRuleRightSide(
        group, transaction, *choice.instance,
        [&values](size_t slot) {
            return slot < values.size() ? values[slot] : rt::kInvalidNodeRef;
        },
        tangents, why, kWholeAnswer, structurallyZero, omitted);
}

/// Whether a `with` parameter of a lifted callee is written into the *ordinary* list of the lift
/// instead of staying a `with` parameter (see `liftSignature` below). One rule, asked by the two
/// writers that have to agree about it -- the signature a callee's lift declares and the argument
/// list its caller fills in: a promoted `with` value is one that has a direction to hand in at all
/// (`Tangent<T>` is defined for it), and a `var` one is never promoted.
///
/// **The question is the type's, not this transform's** (author, 2026-11-04 04:20): a value is a
/// candidate for the ordinary list exactly when it has a tangent space, which is the same predicate
/// every parameter of a lifted signature answers (`Tangent<T>` in `stdlib/types.cml`). The gate that
/// stood here -- "only a `with` value whose tangent is one word wide", i.e. only a `float` -- asked
/// a second question nobody else asks, and its answer was the refusal this transform used to write
/// for an aggregate or an array shape ("`jvp` does not reinterpret a `with` value of type `Point`
/// yet"). A `with` value that is *not* promoted is not given a zero: it has no direction to lose,
/// which is the whole reason it is not promoted (`Tangent<int> = ()`), and it keeps a `with` port of
/// its own with that empty slot beside it.
///
/// A `var` `with` parameter is a cell the body writes. There is no direction to hand in for it (the
/// lift reads it and does not write it, `jvpSignature`'s own sentence), so it keeps the place it had
/// -- a `with` port of its declared value type, with the zero of that type as its tangent.
///
/// **Promotion is a bridge, and it is not a rule** (author, 2026-11-04 07:15). The library's `grad`
/// answers a `with` weight's derivative correctly today because a *callee's* lift promotes that
/// weight into the value list and gives it a direction; deleting the promotion before `grad` is
/// written as a composition over `vjp` would turn right answers into wrong ones, so it stays. Its
/// deadline is the merge that moves the library's `grad`/`with_grad` onto a lambda -- the second half
/// of type-pack unpacking -- and the promotion goes in that same merge, not before it. The criterion
/// for going is that the cases which read a `with` weight's gradient today
/// (`library_grad_of_a_with_weight`, `grad_first_with_parameter`) keep answering the numbers they
/// answer now, with the seed written in the lambda instead of in the lift. Nothing here is a
/// language rule and nothing about it belongs in `docs/design/`: a `with` list is `f`'s own at
/// `jvp(f)`, and promotion happens only inside a callee's lift (`liftSignature`'s `promoteWiths`).
/// The two gaps this bridge holds open, named so that removing it is a check and not a guess:
/// `vjp`'s lambda body cannot call the function value `f`, and a lambda's parameter list cannot
/// write `<w, ...Rest>`. When both are in place, this function and the block above go together.
bool withIsPromoted(const FunctionType *function, size_t index) {
    if (function == nullptr || function->withIsVarAt(index)) {
        return false;
    }
    const camel::core::TangentSpaceAnswer space =
        camel::core::tangentSpaceOf(function->withTypeAt(index));
    return space.hasSpace() && space.space != nullptr;
}

/// The lifted signature of `function`, in the one shape both lifts of this transform need.
///
/// `promoteWiths` is the difference between them, and it is a difference of *where the value comes
/// from*, not of law:
///
///  * the **top-level** lift -- what `jvp(f)` hands a caller -- declares `f`'s `with` list as its
///    own (`<..WithOf<F>>`), the way the declared `Jvp<F>` writes it (`stdlib/types.cml`) and the
///    way `Vjp<F>` forwards it (`vjp(f)` takes `f`'s `with` list, so `split(jvp(f))<..ws>(..xs)`
///    hands the forward half exactly the list it declares). A value captured there is not one of
///    the caller's seeds -- the caller of `jvp(f)` has no argument in which to hand a direction for
///    it -- so its tangent is the zero of its type (`zeroConstantFor` in `liftGraph`), which is
///    design principles 9.9's law for a captured value.
///  * a **callee's** lift -- the `jvp_of_g` this transform makes for a call it meets while lifting
///    somebody else's body -- is called from a graph that is itself lifted, so every value it is
///    handed has a tangent beside it, the call's `with` arguments included. Those values are not
///    captured *by the lifted callee* at all: they arrive at the call. A lifted callee that kept
///    them a `with` list would have nowhere to take the direction in from, and the direction its
///    caller has would be dropped -- a wrong number and not a refusal (measured 2026-11-04 on
///    `vjp((ww) => weighted<ww>(2.0))(3.0)`: the lift answered 9.0 with gradient 0.0 where the
///    engine answers 3.0, because the lifted call handed `jvp_of_weighted` the value of `ww` and
///    the *zero* the old signature left it to write for the tangent).
///
/// So a callee's lift writes every `with` value it can carry a direction for into the ordinary list
/// (`withIsPromoted`), and the two halves keep the one shape every reader of a lifted signature
/// already relies on:
/// `(..ParamsOf<F>, ..WithOf<F>, ..Tangent<ParamsOf<F>>, ..Tangent<WithOf<F>>)`. Two facts about
/// that shape are not this transform's own, and they are why the promoted values go *between* the
/// parameters and their tangents rather than after everything:
///
///  1. the halves have to be **equal in length**, because `split` reads the seam at `count / 2` and
///     takes the first half as the values and the second as the tangents
///     (`split.cpp#valuePortsOf`, `#parameterOfPort`, `#tangentArgumentsOf`). A `with` tangent
///     appended after the parameters' own tangents would move that seam the moment a function has
///     two `with` parameters (`scaled`, `grad_first_with_parameter`'s `sk`/`sx` group: three
///     tangents against one value would put the seam inside the tangents).
///  2. a value with no tangent space still keeps a *slot* in the tangent half -- `()` -- for the
///     reason a parameter does: the halves line up position by position, so a reader that walks
///     them together never has to know which values were left out.
FunctionType *liftSignature(FunctionType *function, bool promoteWiths) {
    if (function == nullptr) {
        return nullptr;
    }
    // What a tangent's type is belongs to the value's type, not to this function: `tangentSpaceOf`
    // is the one answer (camel/core/tangent_space.h), and a float, an array of floats and a tuple of
    // them are each their own space. A result with no tangent space -- an `int`, a `string` -- has
    // nothing to differentiate and is refused here, which is the same refusal as before for every
    // type that was refused, now asked once instead of written out per type.
    const camel::core::TangentSpaceAnswer resultSpace =
        camel::core::tangentSpaceOf(function->exitType());
    if (!resultSpace.hasSpace()) {
        return nullptr;
    }
    auto *lifted = TupleType::create(std::vector<Type *>{function->exitType(), resultSpace.space});
    // The value half, in the order the call passes it: the ordinary parameters first, then the
    // promoted `with` parameters in their declaration order (a `var` one is skipped -- it stays a
    // `with` parameter and has no slot here).
    std::vector<Type *> values;
    values.reserve(function->normTypesCount() + function->withTypesCount());
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        values.push_back(function->normTypeAt(i));
    }
    if (promoteWiths) {
        for (size_t i = 0; i < function->withTypesCount(); ++i) {
            camel::core::type::Type *withType = function->withTypeAt(i);
            if (withType == nullptr) {
                return nullptr;
            }
            if (withIsPromoted(function, i)) {
                values.push_back(withType);
            }
        }
    }
    // Every value becomes two parameters, the value and its tangent, and the two halves are written
    // **values first, then tangents** (author, 2026-11-03): `(..ParamsOf<F>, ..
    // Tangent<ParamsOf<F>>)`, which is the order the declared `Jvp<F>` type writes
    // (`stdlib/types.cml`), so the graph and the type cannot drift apart, and the k-th tangent is
    // the tangent of the k-th value by position rather than by arithmetic on the pairs.
    //
    // `jvp(f)(2.0, 1.0)` passes two numbers, so the graph reads two ports; handing the pair in as one
    // tuple would need a node that takes it apart, and the only such node -- `Dref` -- belongs to
    // graph construction and is refused outright in a runtime graph (src/runtime/graph.cpp).
    //
    // A value with no tangent space -- the `n` of `xs[n]` -- keeps its place in the tangent half as
    // `()`, an empty slot: the two halves line up position by position, so a reader that walks them
    // together never has to know which values were left out. The empty slot is a place a cotangent
    // could be written with nothing to write in it, which is why an earlier reading of this function
    // gave such a parameter no slot at all; that made the halves two lists of different lengths and
    // left the pairing to arithmetic. The author's ruling is the slot.
    camel::core::type::param_vec_t params;
    for (camel::core::type::Type *type : values) {
        params.emplace_back(type, false);
    }
    for (camel::core::type::Type *type : values) {
        const camel::core::TangentSpaceAnswer answer = camel::core::tangentSpaceOf(type);
        params.emplace_back(answer.hasSpace() ? answer.space : emptyTupleType(), false);
    }
    if (params.empty()) {
        return nullptr;
    }
    // What stays a `with` list, in the declaration's order and of its own type: the body reads a
    // weight through a port, and a lifted graph that listed no `with` port would have nowhere for
    // the captured value to enter by (that was the gate this replaces, written as if it were a
    // property of the transform). Under `promoteWiths` only the values that have no direction to
    // hand in are left here -- a `var` cell, and a value with no tangent space (`Tangent<int> = ()`)
    // -- the same ones `values` skipped -- so the two halves of this answer are one decision. A
    // `var` `with` parameter is listed as the variable it was declared as; the lift reads it and
    // does not write it.
    camel::core::type::param_vec_t withs;
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        camel::core::type::Type *withType = function->withTypeAt(i);
        if (withType == nullptr) {
            return nullptr;
        }
        if (promoteWiths && withIsPromoted(function, i)) {
            continue;
        }
        if (function->withIsVarAt(i)) {
            if (camel::core::type::Type *value = camel::core::type::valueTypeOf(withType)) {
                withType = value;
            }
        }
        withs.emplace_back(withType, false);
    }
    // The lifted graph does what the original does to the outside world, so it publishes the same
    // effects: an effectful function's lift is an effectful function (otherwise the run time treats
    // its print as an effect nothing depends on and does not execute it).
    return FunctionType::create(withs, params, lifted)->withResources(function->resources());
}

/// The signature of a **callee's** lift (see `liftSignature`): the same law, with every `with`
/// value that is not a `var` lifted into the ordinary list so that its direction can be handed in
/// at the call. Never the type of `jvp(f)` -- the operator's answer is `jvpSignature`'s
/// (`jvp_operator.cpp#jvpOperatorSignature`), and the two must not be confused: only a callee is
/// called from a graph that has a tangent for its `with` values. This file's own name: no caller
/// outside it lifts a callee, and a declaration in a header would invite one.
FunctionType *jvpCalleeSignature(FunctionType *function) {
    return liftSignature(function, /*promoteWiths=*/true);
}

} // namespace

/// The signature `reinterp<jvp>(f)` has: `f`'s `with` list stays `f`'s own, so this answer and the
/// declared `Jvp<F>` (`stdlib/types.cml`) are one type -- which is what `vjp(f)` forwards to
/// `split(jvp(f))<..ws>(..xs)`. `liftSignature` says why only a callee promotes its `with` values.
FunctionType *jvpSignature(FunctionType *function) {
    return liftSignature(function, /*promoteWiths=*/false);
}

namespace {

/// What a lift hands on to the lifts it starts: the group the members live in, and the callee graphs
/// already lifted, so that a function called twice is lifted once (key: the original graph's id).
struct LiftState {
    const camel::core::context::context_ptr_t &context;
    const camel::runtime::Program *program;
    camel::runtime::GraphDraftGroup &group;
    std::unordered_map<uint32_t, camel::runtime::GraphId> callees;
    /// Whether this lift is a **callee's** (`jvpCalleeSignature`) rather than the top-level one
    /// `jvp(f)` answers (`jvpSignature`). The one place the two differ is where a `with` value's
    /// tangent comes from: the top-level lift writes it as the zero of its type, a callee's lift
    /// reads it from the port its caller fills (see `liftSignature`). Every lift started from a
    /// lifted body is a callee's, and a callee of a callee's is one too, so the flag travels with
    /// the state rather than being decided per graph.
    bool promoteWiths = false;
    /// The lifted signature of each callee this build made, keyed by the **original** graph's id --
    /// the type the call site has to fill in, kept rather than derived again there so the two
    /// cannot disagree about the order or the length of the list (`liftSignature`).
    std::unordered_map<uint32_t, FunctionType *> calleeTypes;
};

bool fail(JvpGraph &refusal) {
    refusal = JvpGraph{};
    return false;
}

bool fail(JvpGraph &refusal, const JvpGraph &why) {
    refusal = why;
    return false;
}

bool liftGraph(
    LiftState &state, const GraphRead &function, std::string_view name,
    camel::runtime::GraphId source, camel::runtime::GraphId member, std::ostream &os,
    JvpGraph &refusal);

} // namespace

/// The build itself, for a caller that has already decided which program the lift's graphs go into:
/// the draft group's. `program` is the program the callee graphs of a body are looked up in (the
/// frozen graph's own for a run-time lift, nothing for one of the lift's own), and `group` is where
/// the lifted graphs are written.
JvpGraph buildJvpGraphInGroup(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GraphDraftGroup &group, const GraphRead &function, std::string_view name,
    camel::runtime::GraphId self, std::ostream &os) {
    const GraphId member = group.reserve(
        "jvp_of_" + std::string(name), "jvp_of_" + std::string(name));
    LiftState state{context, program, group};
    JvpGraph refusal;
    if (!liftGraph(state, function, name, self, member, os, refusal)) {
        return refusal;
    }
    // The count the "one graph per callee" rule is read from: the callee graphs this lift made, in
    // the order they were first called. A function called twice is one entry.
    os << "REINTERP jvp callees=" << state.callees.size() << '\n';
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    return JvpGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

/// The same build for a caller that holds a frozen graph: the face is made here and lives for the
/// call. The name and the identity are the caller's because they are the two facts a draft does
/// not have (it is named and numbered when it is encoded).
JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (function == nullptr) {
        return JvpGraph{};
    }
    const FrozenGraphRead face(*function);
    return buildJvpGraph(context, program, face, function->name(), function->id(), os);
}
JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const GraphRead &function, std::string_view name, camel::runtime::GraphId self,
    std::ostream &os) {
    if (program == nullptr) {
        return JvpGraph{};
    }
    FunctionType *signature = jvpSignature(function.funcType());
    if (signature == nullptr) {
        os << "REINTERP jvp function=" << name
           << " skip=" << refusalCauseName(RefusalCause::Signature) << '\n';
        noteRefusal(RefusalReading("jvp", name, RefusalCause::Signature));
        return JvpGraph{};
    }
    // A function of any number of `float` parameters: every one of them becomes a pair, so the
    // lifted signature is `(v1, d1, ..., vn, dn) => (value, tangent)`. The tangent of the result is
    // the sum of the directions the caller seeded -- one run reads one direction, and a caller that
    // wants `df/dx` and `df/dy` separately runs it twice with (1, 0) and (0, 1).

    camel::runtime::GraphDraftGroup group(context, program);
    return buildJvpGraphInGroup(context, program, group, function, name, self, os);
}
namespace {

bool liftGraph(
    LiftState &state, const GraphRead &function, std::string_view name,
    camel::runtime::GraphId source, camel::runtime::GraphId member, std::ostream &os,
    JvpGraph &refusal) {
    const camel::core::context::context_ptr_t &context = state.context;
    const camel::runtime::Program *program             = state.program;
    camel::runtime::GraphDraftGroup &group             = state.group;
    // Which lift this is decides what happens to a `with` value's tangent (see `liftSignature`):
    // the top-level one `jvp(f)` answers writes the zero of the value's type for it, a callee's one
    // reads it from a port. The type has to be the same answer, or the graph and its call sites
    // would be two different lists.
    FunctionType *signature = state.promoteWiths ? jvpCalleeSignature(function.funcType())
                                                : jvpSignature(function.funcType());
    if (signature == nullptr) {
        return fail(refusal);
    }
    if (state.promoteWiths && rt::isValid(source)) {
        // The call site fills its arguments in from this: the order and the length of the value half
        // are what `liftSignature` decided, and nothing else has to know them.
        state.calleeTypes[rt::toValue(source)] = signature;
    }

    // Steps 1 and 2: a new member with the lifted signature, and one port per parameter, the port
    // itself being the lifted pair `(value, tangent)` the caller passes.
    //
    // The pair is written at the value's own type (`pairTypeOf`): `jvp` of an `f32` function lifts
    // `f32` values into `(f32, f32)`. One instance per type is kept, because a node's type is
    // compared with it by identity where the lift asks "is this chain already the pair?".
    std::unordered_map<const Type *, TupleType *> pairTypes;
    const auto pairOf = [&pairTypes](Type *type) -> TupleType * {
        if (type == nullptr) {
            return nullptr;
        }
        const auto at = pairTypes.find(type);
        if (at != pairTypes.end()) {
            return at->second;
        }
        TupleType *made = pairTypeOf(type);
        pairTypes.emplace(type, made);
        return made;
    };
    // The type of the value the *original* node at `ref` produces: the type its pair is written at.
    const auto valueTypeOf = [&function](gc_node_ref_t ref) -> Type * {
        if (ref >= function.nodeCount()) {
            return nullptr;
        }
        const NodeFace *node = function.node(ref);
        return node != nullptr ? node->dataType() : nullptr;
    };
    // The type of a lifted LOOP node's value, and of the body result that value comes from.
    //
    // The rule is the run time's, not this transform's. With no collection slots a loop's value is
    // the state itself when there is one state, and the tuple of the state types when there are more
    // (`src/runtime/loop.cpp`, the `s == 0` branch: `k == 1 ? stateTypes.front() :
    // TupleType::create(stateTypes)`); a loop that collects puts its tapes after the states in that
    // same tuple. Lifting replaces each state by its pair -- the states keep their places, because a
    // loop has one port per state, so pairing the state tuple as one value would leave the value
    // with fewer components than the states plus the collection slots -- and with one state and
    // nothing collected the whole lifted value is that one pair.
    //
    // The shape a reader of a Fill needs, written down once: the value is `Tuple(pair(s0),
    // pair(s1), ...)`, so reading it inside out gives `s0.v, s0.d, s1.v, s1.d, ...` -- the outer
    // level is the state, the inner level is value-then-tangent -- and the collection slots follow
    // the states, unpaired.
    //
    // A state with no tangent space (an `int` counter) is left exactly as it stands, here and in the
    // body's result alike: `pairOf` is the one question, asked per state, and a state it does not
    // answer for is carried by the loop the way it always was.
    const auto liftedLoopValueType = [&pairOf](Type *valueType, size_t collectionCount,
                                               size_t stateCount) -> Type * {
        if (valueType == nullptr || stateCount == 0) {
            return nullptr;
        }
        if (stateCount == 1 && collectionCount == 0) {
            TupleType *whole = pairOf(valueType);
            return whole != nullptr ? static_cast<Type *>(whole) : valueType;
        }
        if (valueType->code() != camel::core::type::TypeCode::Tuple) {
            return nullptr;
        }
        const auto *tuple  = static_cast<const TupleType *>(valueType);
        const size_t parts = tuple->size();
        if (parts != stateCount + collectionCount) {
            return nullptr;
        }
        std::vector<Type *> lifted;
        lifted.reserve(parts);
        for (size_t j = 0; j < stateCount; ++j) {
            Type *stateType      = tuple->typeAt(j);
            TupleType *statePair = pairOf(stateType);
            lifted.push_back(statePair != nullptr ? static_cast<Type *>(statePair) : stateType);
        }
        for (size_t c = stateCount; c < parts; ++c) {
            lifted.push_back(tuple->typeAt(c));
        }
        return TupleType::create(std::move(lifted));
    };
    // How many states a loop carries: one port per state in its body region, after the iterable's
    // element port when the loop walks an array (`LoopView::iterable` and `LoopView::initialState`
    // are the two, and a `for` body's first port is that round's element).
    const auto loopStateCountOf = [&function](gc_node_ref_t loopRef,
                                              const rt::GCLoopBody &body) -> size_t {
        for (uint32_t region = 1; region < function.regionCount(); ++region) {
            const rt::GCRegion *part = function.region(region);
            if (part == nullptr || part->kind != rt::GCRegionKind::LoopBody ||
                part->owner != loopRef) {
                continue;
            }
            const size_t ports    = function.regionPorts(region).size();
            const bool iterable   = camel::core::loopHasIterable(body.kind);
            return ports > 0 && iterable ? ports - 1 : ports;
        }
        return 0;
    };
    size_t copiedKinds = 0;
    size_t seenKinds   = 0;
    std::vector<std::string> usedRules;
    // The copy of the loop node the walk made, if any.
    gc_node_ref_t liftedLoopNode = camel::runtime::kInvalidNodeRef;
    {
        auto &transaction = group.transaction(member);
        auto &draft       = transaction.edit();
        draft.setFuncType(signature);
        // The tangent a constant or an effect's result gets: the zero of the value's own type. One
        // node per type, because every rule of that type shares it.
        //
        // *Which* types have a zero is `tangentSpaceOf`'s answer, not a list written here: a
        // floating scalar is `Self` and its zero is the one constant its space registers
        // (`zeroTangentConstantOf`, at every width the registry knows -- naming two widths here is
        // how `bf16` and `f16` constants came to stop the lift at `skip=body`), and an array of
        // them is `Self` too -- its zero is an array of the same shape with every element zero,
        // the shape read below because an array's zero is a value and not a constant. Both are
        // the same question asked once (design principles 9.9), and a constant of array type is
        // not an edge case: `f(x) = sum(reshape<[2, 2]>(x) * [[1, 2], [3, 4]])` puts a `float[2, 2]`
        // constant where a rule over an array operand reads it, and a rule is handed the tangent of
        // every operand whose type has one. Answering no zero for it stopped the lift at the first
        // constant it met (`skip=body`), which says nothing about the constant and everything about
        // this gap.
        //
        // The shape comes from the type where the type states it, and from the value where it does
        // not: `[1.0, 2.0] as float[]` is a two-element array whose type says "one dimension, length
        // unknown", and the value in the static slot is what says two. Reading it there is not a
        // second opinion about the type -- the dimension the type leaves open is the one the value
        // fills in, and a dimension neither of them fixes answers no zero.
        std::unordered_map<const Type *, gc_node_ref_t> zeroConstants;
        // The zeros this transform wrote, so a later question can tell "nothing flows here" (a
        // zero) from "a value flows here" (a tangent the lift does not carry). A loop's iterable
        // asks that question; see the `for` loop's input loop below.
        std::unordered_set<gc_node_ref_t> zeroMade;
        const auto zeroConstantFor = [&draft, &zeroConstants, &zeroMade](
                                         Type *type,
                                         const ::Array *shapeOf = nullptr) -> gc_node_ref_t {
            // A zero whose shape was read off one value is that value's zero and is not shared with
            // the next value of the same open type.
            const bool shareable = shapeOf == nullptr;
            if (shareable) {
                const auto at = zeroConstants.find(type);
                if (at != zeroConstants.end()) {
                    return at->second;
                }
            }
            gc_node_ref_t made = rt::kInvalidNodeRef;
            if (const std::optional<slot_t> zero = camel::core::zeroTangentConstantOf(type)) {
                // A space that states its zero as a constant: one machine number in the width the
                // type has. Which widths answer is the registry's list, not this file's.
                made = draft.materializeStaticValue(*zero, type, rt::kGCNodeFlagConstant);
            } else if (type->code() == camel::core::type::TypeCode::Array) {
                const auto *arrayType = static_cast<const camel::core::type::ArrayType *>(type);
                const std::span<const camel::core::type::ExtentForm> stated = arrayType->extents();
                std::vector<int64_t> extents;
                bool exact = arrayType->elemType() != nullptr &&
                             camel::core::type::isFloatingScalar(arrayType->elemType()->code());
                if (exact && stated.empty()) {
                    // No dimension is stated (`T[..]`): the rank is the value's, or there is none.
                    if (shapeOf == nullptr) {
                        exact = false;
                    } else {
                        extents.assign(shapeOf->shape(), shapeOf->shape() + shapeOf->rank());
                    }
                }
                for (size_t i = 0; exact && i < stated.size(); ++i) {
                    if (const std::optional<int64_t> size = stated[i].evaluate({})) {
                        extents.push_back(*size);
                    } else if (shapeOf != nullptr && i < shapeOf->rank()) {
                        extents.push_back(shapeOf->shape()[i]);
                    } else {
                        exact = false;
                    }
                }
                if (exact) {
                    ::Array *zero = ::Array::create(
                        mm::autoSpace(), arrayType, std::span<const int64_t>(extents),
                        /*zeroInit=*/true);
                    if (zero != nullptr) {
                        made = draft.materializeStaticValue(
                            camel::core::rtdata::toSlot<::Array *>(zero), type,
                            rt::kGCNodeFlagConstant);
                    }
                }
            }
            if (shareable) {
                zeroConstants.emplace(type, made);
            }
            if (made != rt::kInvalidNodeRef) {
                zeroMade.insert(made);
            }
            return made;
        };
        // The zero tangent of a value that is not a constant -- a captured value or a `with` parameter --
        // and where that zero is made.
        //
        // *Why it is zero.* This road differentiates the function's ordinary parameters: the caller
        // hands a seed for each of them, and a value the body merely reads is not one of those
        // inputs, so it does not move. Design principles 9.9 says the same thing as a rule
        // (`reinterpret<jvp>` lifts a captured value to a zero tangent), and the forward transform
        // therefore adds no second port to hand it a direction: the value has one port and the
        // tangent is the zero of its own type.
        //
        // *How it is made, and the boundary between the two ways.* A type whose extents are all
        // stated has a zero that can be written down while the graph is built -- `0.0` for a
        // `float`, an array of zeros for `float[2, 2]` -- and `zeroConstantFor` above writes it, as
        // it already does for a constant. A type that leaves an extent open (`float[]`, `f32[..]`,
        // which is how S2's own parameter tree is written) has no such zero: the length is the
        // *value's*, and the value is a port that has not been run yet. Only then is the zero made
        // where the value is, and by the operator the type's tangent space already names
        // (`TangentSpace::zerosLikeUri`, include/camel/core/derivative.h; the arrays' is
        // `:tangent/zeros_arr`, src/builtin/operators/derivative.cpp) -- the same zero every other
        // reader of a tangent already asks for (`zeroTangent`, src/builtin/operators/struct.cpp).
        //
        // The order matters and is not an optimisation to be traded away: the static zero is one
        // node per type shared by every rule of that type, while a zero built from a value is that
        // value's and cannot be shared with the next value of the same open type. Reaching for the
        // registry first would also make every `float` tangent a call, which is neither what the
        // rules were handed before nor what a reader of the graph sees today.
        const auto zeroOfValueAtRunTime = [&draft, &zeroMade](gc_node_ref_t primal,
                                                              Type *type) -> gc_node_ref_t {
            if (type == nullptr || type->code() != camel::core::type::TypeCode::Array) {
                return rt::kInvalidNodeRef;
            }
            const auto *space = camel::core::DerivativeRegistry::instance().findTangentSpace(type);
            if (space == nullptr || space->zerosLikeUri.empty()) {
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t zero = draft.addOperNode(type, nullptr, space->zerosLikeUri);
            draft.setNormInputs(zero, std::span<const gc_node_ref_t>(&primal, 1));
            // A zero this transform wrote, like the static ones above: the value whose shape it was
            // read from does not move in forward mode, so a loop over it has no element tangent
            // either, and the loop must stay liftable.
            zeroMade.insert(zero);
            return zero;
        };
        // The graph this one is a lift of: a fact about the graph, not a comment about the call.
        // `split` reads it when it has to carry a call of a lifted graph into a half, so that the
        // value half can call the primal instead of the lift (whose first parameter is the pair).
        // Every member the lift makes gets it, the top-level `jvp_of_f` and each `jvp_of_callee`.
        // The id is the caller's, like the name: a draft is numbered when it is encoded.
        draft.addLiftedFromRef(source);
        // One pair of ports per value of the lifted signature, in its order: the value the caller
        // passes and the seed of that direction. A port node is a node; the graph's *parameters* are
        // the port nodes it lists as its norm ports, and a call is checked against that list ("has 0
        // norm ports" is how a graph with the nodes but no entries in the list refused the call) --
        // so the lifted signature's parameters are exactly these ports, in the same order.
        const size_t parameterCount = function.funcType()->normTypesCount();
        const size_t withCount =
            function.funcType() != nullptr ? function.funcType()->withTypesCount() : 0;
        const std::vector<gc_node_ref_t> originalWith = function.withPorts();
        // The value half of the lifted list: the ordinary parameters in their order, and then -- for
        // a **callee's** lift only -- every `with` value that is not a `var`, in the declaration's
        // order (`liftSignature`). Which slot each one takes is decided here once and read below
        // twice: the signature writes the same list, and the call site fills it in by these indices.
        // `promotedSlotOf[i]` is the place `with` parameter `i` takes among the values, or
        // `kNotPromoted` when it keeps a `with` port of its own.
        constexpr size_t kNotPromoted = static_cast<size_t>(-1);
        std::vector<size_t> promotedSlotOf(withCount, kNotPromoted);
        std::vector<camel::core::type::Type *> valueTypes;
        valueTypes.reserve(parameterCount + withCount);
        for (size_t i = 0; i < parameterCount; ++i) {
            valueTypes.push_back(function.funcType()->normTypeAt(i));
        }
        if (state.promoteWiths) {
            for (size_t i = 0; i < withCount; ++i) {
                camel::core::type::Type *withType = function.funcType()->withTypeAt(i);
                if (withType == nullptr || !withIsPromoted(function.funcType(), i)) {
                    continue;
                }
                promotedSlotOf[i] = valueTypes.size();
                valueTypes.push_back(withType);
            }
        }
        std::vector<gc_node_ref_t> valuePorts;
        std::vector<gc_node_ref_t> tangentPorts;
        valuePorts.reserve(valueTypes.size());
        tangentPorts.reserve(valueTypes.size());
        // The value half first, in the list's order, and then the tangent half in the same order: the
        // lifted signature is `(..values, ..their tangents)` (author, 2026-11-03), so the ports of the
        // graph line up with the declared type by position -- port k is the k-th value, port n + k is
        // its tangent. A caller copies its arguments into these in order, so the order here *is* the
        // interface.
        for (camel::core::type::Type *valueType : valueTypes) {
            // The port's type is the value's own. The tangent port's is that value's tangent space --
            // `tangentSpaceOf`'s answer, not `float`: a `float[]` parameter's tangent is an array of
            // the same shape, and a rule that consumes it (`index_into`) takes it whole. The
            // element's tangent only appears one layer down, at `xs[i]`.
            const gc_node_ref_t value =
                draft.addPortNode(valueType, draft.allocateRuntimeSlot(valueType));
            draft.appendNormPort(value);
            valuePorts.push_back(value);
        }
        for (camel::core::type::Type *valueType : valueTypes) {
            const camel::core::TangentSpaceAnswer tangentSpace =
                camel::core::tangentSpaceOf(valueType);
            // A value with no tangent space takes a `()` slot here too, and the slot is a port: the
            // two halves keep their places position by position, so a reader that walks them
            // together never has to know which values were left out. What such a value does *not*
            // get is a tangent to walk with -- `tangentPorts` holds the invalid ref, so the chain
            // below reads "nothing to differentiate" and answers a zero contribution rather than a
            // `()`-typed node where a direction belongs.
            camel::core::type::Type *tangentType =
                tangentSpace.hasSpace() ? tangentSpace.space : emptyTupleType();
            const gc_node_ref_t slot =
                draft.addPortNode(tangentType, draft.allocateRuntimeSlot(tangentType));
            draft.appendNormPort(slot);
            tangentPorts.push_back(tangentSpace.hasSpace() ? slot : rt::kInvalidNodeRef);
        }
        // Which value of the lifted list a port of the original body is, by its place in that list.
        std::unordered_map<gc_node_ref_t, size_t> parameterOf;
        const std::vector<gc_node_ref_t> originalPorts = function.normPorts();
        for (size_t i = 0; i < originalPorts.size(); ++i) {
            parameterOf[originalPorts[i]] = i;
        }
        // A promoted `with` parameter is a parameter of the lifted graph like any other: its port
        // takes the value slot decided above, and the tangent beside it is the direction its caller
        // hands in. That is the whole of what lets a lifted body that reads a weight through a port
        // pass the direction it has for that weight on to the callee it calls (`GCNodeKind::Func`
        // below) -- without it the callee's chain is constant in the weight and its `lin` is zero.
        for (size_t i = 0; i < withCount && i < originalWith.size(); ++i) {
            if (promotedSlotOf[i] != kNotPromoted) {
                parameterOf[originalWith[i]] = promotedSlotOf[i];
            }
        }
        // The `with` parameters that stay `with` parameters -- a `var` cell, and a value with no
        // tangent space, in every lift -- get a port each, listed among the lifted graph's `with`
        // ports, so the captured value enters the copy by a port rather than by a node nothing can
        // fill. One per such parameter, in the declaration's order -- a port node that the graph does
        // not list as a `with` port is a hole, which is exactly how the refusal this replaces
        // described the problem. The value only: the tangent of a captured value is the zero below,
        // so there is no second port to hand a direction in.
        //
        // Nothing is refused here any more (author, 2026-11-04 04:20). A `with` value is *not*
        // promoted exactly when it has no direction to lose -- it is a `var` cell, or its type has
        // no tangent space (`Tangent<int> = ()`) -- so a `with` port with the zero beside it is the
        // whole truth about it, and the refusal that stood here ("`jvp` does not reinterpret a
        // `with` value of type `Point` yet") was a distance-to-the-cut written into the lift. A
        // shape that does have a direction is promoted (`withIsPromoted`) and the cut is the one
        // that says what it takes.
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> withPortOf;
        // The declared ones and the captured ones, one list: the first `withTypesCount()` are the
        // caller's `with` arguments and the rest are the values the body captured, in the closure
        // tuple's order (`GraphRead::withPortsOf`, cited below).
        {
            for (size_t i = 0; i < withCount && i < originalWith.size(); ++i) {
                if (promotedSlotOf[i] != kNotPromoted) {
                    continue;
                }
                camel::core::type::Type *withType = function.funcType()->withTypeAt(i);
                if (withType == nullptr) {
                    continue;
                }
                if (function.funcType()->withIsVarAt(i)) {
                    if (camel::core::type::Type *value = camel::core::type::valueTypeOf(withType)) {
                        withType = value;
                    }
                }
                const gc_node_ref_t port =
                    draft.addPortNode(withType, draft.allocateRuntimeSlot(withType));
                draft.appendWithPort(port);
                withPortOf[originalWith[i]] = port;
            }
        }
        // The values the body *captures* from where it was written, which are not `with` parameters
        // and were the one kind of port this lift left out. A capture is a port of the graph
        // (`GraphRead::closureNodesOf`, which is `GCGraph::closureNodes`) but not one of its `with`
        // ports, so a cell the walk never made is a hole: the lifted copy read nothing where the
        // value stood, and the refusal it produced named the whole body (`skip=body`) rather than
        // the capture -- measured 2026-11-03 on `(t: float[]) => k * t[0]`, which refused for a
        // captured `float` exactly as it did for a captured `float[]`, so the type of the captured
        // value was never the question and `pairTypeOf` was never the gate.
        //
        // The copy declares them in the source's order, which is the closure tuple's order, and
        // takes the source's closure type; the function value this transform answers already
        // carries `f`'s tuple (`jvp_operator.cpp`, `Function::create` with `f`'s tuple and the tuple
        // copied over), so the two together make a read through the derivative read the caller's
        // value. `rematKernel` (modules/transforms/remat_operator.cpp) copies a graph the same way
        // and records the same defect: a capture the copy does not declare answers the zero of its
        // slot.
        //
        // `closureNodesOf` and `closureTypeOf` are two *readings* added to `GraphRead`, not new
        // storage: a runtime graph has answered both since captures existed (`GCGraph::closureNodes`
        // and `GCGraph::closureType`), and the compile-time draft answers both as well
        // (`GraphDraft::closureNodes`, `GraphDraft::closureType`). The face that turns a graph into
        // what a transform reads simply did not pass them on, so a transform holding a `GraphRead`
        // could see a graph's parameters and its `with` ports and not what it captures. Nothing is
        // recorded twice and nothing new is encoded.
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> capturePortOf;
        {
            const std::vector<gc_node_ref_t> captures = function.closureNodesOf();
            for (gc_node_ref_t original : captures) {
                const NodeFace *captureNode = function.node(original);
                camel::core::type::Type *captureType =
                    captureNode != nullptr ? captureNode->dataType() : nullptr;
                if (captureType == nullptr) {
                    continue;
                }
                const gc_node_ref_t port =
                    draft.addPortNode(captureType, draft.allocateRuntimeSlot(captureType));
                draft.appendClosureNode(port);
                capturePortOf[original] = port;
            }
            if (!capturePortOf.empty()) {
                if (const camel::core::type::TupleType *closureType = function.closureTypeOf()) {
                    draft.setClosureType(
                        const_cast<camel::core::type::TupleType *>(closureType));
                }
            }
        }
        // Step 3, first layer: the body is copied node by node. A port reads the caller's value, a
        // constant is written again with the same value, and a primitive keeps its operator and gets
        // its inputs from the copies. Nothing is differentiated here -- no rule closure is built --
        // so the chain reproduces the original value; the tangent is still the caller's.
        // NodeIds are 0 .. nodeCount() - 1 (include/camel/runtime/graph.h), so the walk starts at 0:
        // starting at 1 skipped the port, and a primitive that read it then read node 0 of the new
        // graph -- the value came out of an input nobody had copied.
        std::vector<gc_node_ref_t> copyOf(function.nodeCount(), rt::kInvalidNodeRef);
        std::vector<bool> copied(function.nodeCount(), false);
        // The value a node hands to a reader: the copy itself, except where the copy is a lifted
        // pair -- a loop's state port -- and the reader wants the value half.
        std::vector<gc_node_ref_t> valueOf(function.nodeCount(), rt::kInvalidNodeRef);
        // The nodes whose *copy* is the pair itself: a lifted call, and a loop with one state and
        // nothing collected (its value is that state, so the pair is the value). Written down
        // because a type cannot say it: with two states a loop's value is `Tuple(pair, pair)`,
        // which is the same type as the pair of the whole value, and only this set tells the exit
        // which of the two it is holding.
        std::unordered_set<gc_node_ref_t> pairIsTheCopy;
        // What each tuple fill `jvp` writes is made of, member by slot: a loop's value tuple and the
        // tangent tuple beside it, and the copy of any tuple a body builds. A reader that projects one
        // member of such a tuple does not need the tuple: the member is a node already (the loop's
        // half, the body's own value), so the projection answers that node instead of indexing a fill
        // nobody else may read. Measured 2026-11-03: without this, `(loop t = (x, x) ...).1` reached
        // both of the loop's components through the fill it built, which is exactly what `split`
        // cannot cut. A slot the payload does not list (it takes the template's constant) is left out
        // and its projection is copied as the projection it is.
        std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> membersOfFill;
        bool complete = true;
        // Step 3, second layer: a tangent for every node. A constant's tangent is zero, a port's is
        // the tangent parameter the caller passed, and a primitive's is the forward rule the ruleset
        // files for its operator, applied to its operands' values and tangents -- read from the
        // index at this moment, not from a table written here. A primitive with no rule is refused
        // *by name* rather than answered with 0 or 1, because a wrong derivative that looks like a
        // derivative is the expensive kind of wrong.
        std::vector<gc_node_ref_t> tangentOf(function.nodeCount(), rt::kInvalidNodeRef);
        // A FILL node over `members`, in order: the static template is the Norm input (the shape the
        // fill copies) and the members are the context ports. This is the shape every writer of a
        // tuple in a runtime graph uses (`split`'s `fillTuple`, split.cpp), so a tuple this lift
        // makes is a tuple the other writers can read. The payload is the caller's, because a FILL
        // carries more than a tuple (an array, a struct, a closure) and the kind and slot list of
        // the node being copied are what make the copy the same node.
        const auto fillWithPayload = [&draft](
                                         Type *type, std::span<const std::byte> payload,
                                         std::span<const gc_node_ref_t> members) -> gc_node_ref_t {
            // The template is the shape the fill reads the slots it does not list from: a tuple of
            // that arity for a tuple, an array of that length for an array. `Array::create` takes
            // the element type from the array type, so the template's elements are the type the
            // members are written at; a tuple of two and an array of two are different shapes, and
            // a template of the wrong one is written into a value that holds one element (measured:
            // an array state's tangent, "Array flat index out of range: write of element 1 of an
            // array that holds 1").
            const slot_t templateSlot =
                type != nullptr && type->code() == camel::core::type::TypeCode::Array
                    ? camel::core::rtdata::toSlot(::Array::create(
                          mm::autoSpace(), static_cast<ArrayType *>(type), members.size()))
                    : camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(
                          ::Tuple::create(members.size(), mm::autoSpace()));
            const gc_node_ref_t source = draft.materializeStaticValue(templateSlot, type);
            const gc_node_ref_t fill = draft.addFillNode(type, payload);
            draft.setNormInputs(fill, std::span<const gc_node_ref_t>(&source, 1));
            draft.setWithInputs(fill, members);
            return fill;
        };
        /// The same FILL for a tuple whose payload this lift writes itself: the slots are 0 .. n-1.
        const auto fillTupleOf = [&fillWithPayload](
                                     Type *tupleType,
                                     std::span<const gc_node_ref_t> members) -> gc_node_ref_t {
            std::vector<size_t> slots(members.size());
            for (size_t i = 0; i < slots.size(); ++i) {
                slots[i] = i;
            }
            const std::vector<std::byte> payload =
                rt::makeFillPayload(rt::GCFillKind::Tuple, slots);
            return fillWithPayload(tupleType, payload, members);
        };
        // The lifted pair: a value and its tangent, in the one shape the lifted types are written in
        // -- the pair of the value's own type, which is the caller's answer. The exit of the
        // transformed body and the exit of an arm are both this pair, so it is made in one place.
        const auto liftedPair = [&pairOf, &fillTupleOf](
                                    Type *valueType, gc_node_ref_t half,
                                    gc_node_ref_t halfTangent) {
            TupleType *pairType = pairOf(valueType);
            const std::array<gc_node_ref_t, 2> halves{half, halfTangent};
            return fillTupleOf(pairType, halves);
        };
        // The body's result of a LOOP node and the loop's own value are the same shape (see
        // `liftedLoopValueType`): the state values first, then what the collection slots keep. The
        // state components are the pair -- one per component -- and the collection components are
        // spliced back as they stand, so the node's type and the body's result cannot contradict
        // each other: both are `liftedLoopValueType` of their own tuple.
        const auto liftedLoopBodyResult = [&draft, &valueTypeOf, &pairOf, &liftedPair, &fillTupleOf,
                                           &liftedLoopValueType, &copyOf, &tangentOf](
                                              gc_node_ref_t exit, size_t collectionCount,
                                              size_t stateCount) -> gc_node_ref_t {
            Type *exitType = valueTypeOf(exit);
            if (exitType == nullptr || exitType->code() != camel::core::type::TypeCode::Tuple) {
                return rt::kInvalidNodeRef;
            }
            const auto *tuple = static_cast<const TupleType *>(exitType);
            if (tuple->size() != stateCount + collectionCount) {
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t exitCopy    = copyOf[exit];
            const gc_node_ref_t exitTangent = tangentOf[exit];
            if (exitCopy == rt::kInvalidNodeRef || exitTangent == rt::kInvalidNodeRef) {
                return rt::kInvalidNodeRef;
            }
            Type *liftedType = liftedLoopValueType(exitType, collectionCount, stateCount);
            if (liftedType == nullptr ||
                liftedType->code() != camel::core::type::TypeCode::Tuple) {
                return rt::kInvalidNodeRef;
            }
            std::vector<gc_node_ref_t> members;
            members.reserve(tuple->size());
            for (size_t j = 0; j < stateCount; ++j) {
                // The j-th state, projected out of the body's result and its tangent: the same two
                // halves a straight-line exit hands `liftedPair`. A state with no pair is spliced
                // back as it stands, which is what the value type says of it too.
                const gc_node_ref_t half = draft.addAccsNode(
                    tuple->typeAt(j), static_cast<uint32_t>(j));
                draft.setNormInputs(half, std::span<const gc_node_ref_t>(&exitCopy, 1));
                if (pairOf(tuple->typeAt(j)) == nullptr) {
                    members.push_back(half);
                    continue;
                }
                const gc_node_ref_t halfTangent =
                    draft.addAccsNode(tuple->typeAt(j), static_cast<uint32_t>(j));
                draft.setNormInputs(halfTangent, std::span<const gc_node_ref_t>(&exitTangent, 1));
                members.push_back(liftedPair(tuple->typeAt(j), half, halfTangent));
            }
            for (size_t c = 0; c < collectionCount; ++c) {
                const size_t index       = stateCount + c;
                const gc_node_ref_t kept = draft.addAccsNode(
                    tuple->typeAt(index), static_cast<uint32_t>(index));
                draft.setNormInputs(kept, std::span<const gc_node_ref_t>(&exitCopy, 1));
                members.push_back(kept);
            }
            return fillTupleOf(liftedType, members);
        };
        // A branch is a property of the *graph*, not of a node: its arms are regions, the value they
        // yield is the Join's, and an arm yields the pair of that value in the lifted graph -- so the
        // Join's type is the pair of the value it yields, and the Join has to exist before the Brch
        // that names it. Regions and the two nodes are made here; the walk copies nodes as it does
        // anywhere else; the wiring (condition, arm exits, back-references) is done after it, from
        // the original graph's own structure. The action *sequence* is the one every writer of a
        // branch uses (src/passes/opt/defun/defunctionalize.cpp); what a branch means for `jvp` is
        // decided here and nowhere else.
        std::vector<uint32_t> liftedRegion(
            function.regionCount() > 0 ? function.regionCount() : 1, 0);
        for (uint32_t r = 1; r < function.regionCount(); ++r) {
            const rt::GCRegion *from = function.region(r);
            if (from == nullptr) {
                continue;
            }
            const uint32_t parent =
                from->parent < liftedRegion.size() ? liftedRegion[from->parent] : 0;
            liftedRegion[r] = draft.addRegion(from->kind, parent, rt::kInvalidNodeRef, from->index);
        }
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> liftedJoin; // the original Join -> its copy
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> joinOfBrch; // a Brch -> the Join it names
        for (gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Brch) {
                continue;
            }
            rt::GCBrchBody brchBody{};
            const auto *body = function.bodyOf(ref, brchBody) ? &brchBody : nullptr;
            if (body == nullptr || body->join == rt::kInvalidNodeRef) {
                continue;
            }
            const NodeFace *join = function.node(body->join);
            if (join == nullptr) {
                continue;
            }
            // The Join's type is the pair of the value it yields: an arm yields the pair in the
            // lifted graph, so the node that joins the arms is the pair's type -- the value's own
            // (`pairTypeOf`), not one type written for the whole transform.
            TupleType *joinPairType = pairOf(join->dataType());
            Type *joinType = joinPairType != nullptr ? joinPairType : join->dataType();
            const gc_node_ref_t made = draft.addJoinNode(joinType, rt::kInvalidNodeRef, body->armCount);
            liftedJoin[body->join]   = made;
            joinOfBrch[ref]          = made;
            if (joinPairType != nullptr) {
                // The Join carries the pair itself: an arm writes the pair into it, so a reader of the
                // join -- the exit among them -- already holds the pair and must not take it apart.
                pairIsTheCopy.insert(body->join);
            }
        }
        // A function value is made before the walk, not when the walk reaches it: a call names its
        // callee through the *value*, and the call may be written before the value in node order.
        // Each original callee graph is lifted once (state.callees), which is what makes two calls
        // of one function one graph.
        std::unordered_map<gc_node_ref_t, GraphId> liftedCalleeOf;
        auto liftFuncValue = [&](gc_node_ref_t ref) -> bool {
            const GraphId calleeId = function.calleeIdOf(ref);
            if (!rt::isValid(calleeId)) {
                return false;
            }
            camel::runtime::GCGraph *callee = program->graph(calleeId);
            if (callee == nullptr) {
                return false;
            }
            // The callee is read through the same kind of face as the caller: a frozen graph here,
            // because this build starts from one.
            const FrozenGraphRead calleeFace(*callee);
            const std::string_view calleeName = callee->name();
            GraphId liftedCallee;
            const auto at = state.callees.find(rt::toValue(calleeId));
            if (at == state.callees.end()) {
                liftedCallee = group.reserve(
                    "jvp_of_" + std::string(callee->name()),
                    "jvp_of_" + std::string(callee->name()));
                state.callees.emplace(rt::toValue(calleeId), liftedCallee);
                // The lift this starts is a **callee's**: it is called from a body that is itself
                // lifted, so the values the call passes it -- its `with` arguments among them --
                // arrive with the tangents the caller's chain already carries. That is what
                // `promoteWiths` decides, and it is a property of the *call* the lift is for, not
                // of the callee: the same function lifted for `jvp(f)` at the top level keeps its
                // `with` list (`liftSignature`).
                const bool outerPromote = state.promoteWiths;
                state.promoteWiths    = true;
                JvpGraph sub;
                const bool lifted =
                    liftGraph(state, calleeFace, calleeName, calleeId, liftedCallee, os, sub);
                state.promoteWiths = outerPromote;
                if (!lifted) {
                    refusal = sub;
                    return false;
                }
            } else {
                liftedCallee = at->second;
            }
            // The signature the call site fills in has to be the one the callee's lift declared, and
            // it is already there (the lift recorded it): asking for it again is how the two could
            // drift apart.
            FunctionType *calleeSignature = jvpCalleeSignature(callee->funcType());
            if (calleeSignature == nullptr) {
                return false;
            }
            // The call reaches the lifted callee through this node, so the callee is a dependency of this
            // graph by the reference table's own rule (a FUNC node names its callee).
            liftedCalleeOf[ref] = liftedCallee;
            draft.addDependency(liftedCallee);
            return true;
        };
        for (gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Func) {
                continue;
            }
            if (!liftFuncValue(ref)) {
                // Kept, not replaced: when the callee's own lift refused it left its refusal here
                // (`refusal = sub` above, a named construct and the node it stands at), and building
                // an empty one from nothing would report "this function has no forward transform"
                // about a shape the transform can name.
                return false;
            }
        }
        for (std::uint8_t kind = 0; kind <= static_cast<std::uint8_t>(rt::GCNodeKind::Dref); ++kind) {
            bool seen = false;
            for (camel::runtime::gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
                const NodeFace *node = function.node(ref);
                if (node != nullptr && static_cast<std::uint8_t>(node->kind) == kind) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                ++seenKinds;
            }
        }
        // The *element* port of a `for` loop's body region: the value one round takes out of the
        // array the loop iterates. It is not the loop's state, and it is not lifted into a pair --
        // the array it comes from is not a differentiated value (design principles 9.9: a loop may
        // read an iterable, and the gradient stops at the read), so the round's element arrives as
        // it stands and its tangent is the zero of its own type. Pairing it -- which `pairOf` does
        // for any `float` -- makes the body take `(float, float)` out of an array of `float`, and
        // no back end accepts a loop whose element type and array element type differ
        // (`Runtime graph ... has LOOP node N: its body takes elements of type '(float, float)'
        // from an array of 'float'`).
        std::unordered_set<gc_node_ref_t> loopElementPorts;
        for (gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Loop) {
                continue;
            }
            rt::GCLoopBody loopBody{};
            if (!function.bodyOf(ref, loopBody) ||
                loopBody.kind == camel::core::LoopKind::While) {
                continue;
            }
            for (uint32_t region = 0; region < function.regionCount(); ++region) {
                const rt::GCRegion *part = function.region(region);
                if (part == nullptr || part->owner != ref ||
                    part->kind != rt::GCRegionKind::LoopBody) {
                    continue;
                }
                const std::vector<gc_node_ref_t> ports = function.regionPorts(region);
                if (!ports.empty()) {
                    loopElementPorts.insert(ports.front());
                }
            }
        }
        // The ports of a loop's body, made before the walk. A body's ports are the round's element
        // and the values it carries, and the front end can write them *after* the nodes that read
        // them: a tuple state becomes one variable per component when the region is closed, so the
        // port `s.0` reads can land past the `+` that reads it (measured: `:op/mul_d` at node 5
        // reading port 11 in a body of five nodes). The walk is in node order (above), and a reader
        // whose operand is not made yet refuses the whole lift -- a refusal that says "this
        // function has no forward transform" about a body that is perfectly differentiable. The
        // ports are therefore made here, the same "made before the walk" a join gets, and the walk's
        // port case finds them and only wires them.
        for (uint32_t region = 1; region < function.regionCount(); ++region) {
            const rt::GCRegion *part = function.region(region);
            if (part == nullptr || part->kind != rt::GCRegionKind::LoopBody) {
                continue;
            }
            if (region < liftedRegion.size()) {
                draft.setInsertRegion(liftedRegion[region]);
            }
            for (gc_node_ref_t port : function.regionPorts(region)) {
                if (port >= function.nodeCount() || copied[port]) {
                    continue;
                }
                const NodeFace *portNode = function.node(port);
                if (portNode == nullptr || portNode->kind != rt::GCNodeKind::Port) {
                    continue;
                }
                Type *portType = portNode->dataType();
                if (loopElementPorts.contains(port)) {
                    // The round's element: a value the array hands in, with no tangent of its own.
                    copyOf[port] = draft.addPortNode(portType, draft.allocateRuntimeSlot(portType));
                    if (pairOf(portType) != nullptr) {
                        tangentOf[port] = zeroConstantFor(portType);
                    }
                } else {
                    // A carried state: the port takes the pair, and the body reads the two halves
                    // through projections -- the one shape a lifted loop's region can have.
                    TupleType *statePair = pairOf(portType);
                    Type *type = statePair != nullptr ? static_cast<Type *>(statePair) : portType;
                    copyOf[port] = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
                    if (statePair != nullptr) {
                        const gc_node_ref_t pairOfState = copyOf[port];
                        const gc_node_ref_t half        = draft.addAccsNode(portType, 0);
                        draft.setNormInputs(half, std::span<const gc_node_ref_t>(&pairOfState, 1));
                        const gc_node_ref_t halfTangent = draft.addAccsNode(portType, 1);
                        draft.setNormInputs(
                            halfTangent, std::span<const gc_node_ref_t>(&pairOfState, 1));
                        valueOf[port]   = half;
                        tangentOf[port] = halfTangent;
                    }
                }
                copied[port] = true;
                ++copiedKinds;
            }
        }
        draft.setInsertRegion(0);
        for (camel::runtime::gc_node_ref_t ref = 0; ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr) {
                continue;
            }
            // A node written inside an arm is copied into that arm: the arms stay arms, and an
            // operator only one arm uses is not computed for the arm that was not taken.
            const uint32_t region = function.nodeRegion(ref);
            draft.setInsertRegion(
                region != 0 && region < liftedRegion.size() ? liftedRegion[region] : 0);
            switch (node->kind) {
            case rt::GCNodeKind::Port: {
                if (copied[ref]) {
                    // A loop body's port, made before the walk so that a reader whose port is
                    // written after it finds it there (see the pre-pass above).
                    break;
                }
                // A parameter of the body is one of the caller's pairs: the value is the first port
                // of that pair, the tangent -- the seed of that direction -- is the second. The
                // chain then adds every direction's contribution, so the exit is the derivative in
                // the direction the caller seeded.
                const auto parameter = parameterOf.find(ref);
                if (parameter != parameterOf.end() && parameter->second < valuePorts.size()) {
                    copyOf[ref]    = valuePorts[parameter->second];
                    tangentOf[ref] = tangentPorts[parameter->second]; // invalid when the parameter has no tangent space
                    copied[ref]    = true;
                    ++copiedKinds;
                    break;
                }
                // A `with` parameter of the body is the port made for it above: the caller's
                // captured value, read as the body read it. Its tangent is the zero of its own type
                // -- a captured value does not move in forward mode (design principles 9.9's
                // ruleset: `reinterpret<jvp>` lifts a captured value to a zero tangent) -- so a
                // primitive that reads it is handed that zero instead of being refused for an
                // operand that never arrived.
                const auto withPort = withPortOf.find(ref);
                if (withPort != withPortOf.end()) {
                    copyOf[ref] = withPort->second;
                    if (camel::core::tangentSpaceOf(node->dataType()).hasSpace()) {
                        tangentOf[ref] = zeroConstantFor(node->dataType());
                        if (tangentOf[ref] == rt::kInvalidNodeRef) {
                            tangentOf[ref] = zeroOfValueAtRunTime(copyOf[ref], node->dataType());
                        }
                    }
                    copied[ref] = true;
                    ++copiedKinds;
                    break;
                }
                // A value the body captured from where it was written: it enters by the port made
                // for it above, and it does not move -- the same rule the `with` parameter one arm
                // up follows (design principles 9.9's ruleset: `reinterpret<jvp>` lifts a captured
                // value to a zero tangent), so its tangent is the zero of its own type and there is
                // no second port to hand a direction in.
                const auto captured = capturePortOf.find(ref);
                if (captured != capturePortOf.end()) {
                    copyOf[ref] = captured->second;
                    if (camel::core::tangentSpaceOf(node->dataType()).hasSpace()) {
                        tangentOf[ref] = zeroConstantFor(node->dataType());
                        if (tangentOf[ref] == rt::kInvalidNodeRef) {
                            // An extent the type leaves open: the zero is the value's shape, made
                            // where the value is (see `zeroOfValueAtRunTime`).
                            tangentOf[ref] = zeroOfValueAtRunTime(copyOf[ref], node->dataType());
                        }
                    }
                    copied[ref] = true;
                    ++copiedKinds;
                    break;
                }
                // A port of a loop's body region is a loop variable, and one the transform carries a
                // pair for is the carried state: the loop carries the pair, so this port is a pair
                // port and the body reads its halves through projections.
                const uint32_t portRegion = function.nodeRegion(ref);
                const rt::GCRegion *portRegionBody =
                    portRegion != 0 ? function.region(portRegion) : nullptr;
                if (loopElementPorts.contains(ref)) {
                    // The round's element: a value the array hands in, with no tangent of its own.
                    copyOf[ref] = draft.addPortNode(
                        node->dataType(), draft.allocateRuntimeSlot(node->dataType()));
                    if (pairOf(node->dataType()) != nullptr) {
                        tangentOf[ref] = zeroConstantFor(node->dataType());
                    }
                    copied[ref] = true;
                    ++copiedKinds;
                    break;
                }
                if (portRegionBody != nullptr &&
                    portRegionBody->kind == rt::GCRegionKind::LoopBody) {
                    TupleType *statePair           = pairOf(node->dataType());
                    const bool carried             = statePair != nullptr;
                    camel::core::type::Type *portType =
                        carried ? static_cast<Type *>(statePair) : node->dataType();
                    copyOf[ref] = draft.addPortNode(portType, draft.allocateRuntimeSlot(portType));
                    if (carried) {
                        const gc_node_ref_t pairOfState = copyOf[ref];
                        const gc_node_ref_t half =
                            draft.addAccsNode(node->dataType(), 0);
                        draft.setNormInputs(half, std::span<const gc_node_ref_t>(&pairOfState, 1));
                        const gc_node_ref_t halfTangent =
                            draft.addAccsNode(node->dataType(), 1);
                        draft.setNormInputs(
                            halfTangent, std::span<const gc_node_ref_t>(&pairOfState, 1));
                        valueOf[ref]   = half;
                        tangentOf[ref] = halfTangent;
                    }
                    copied[ref] = true;
                    ++copiedKinds;
                } else {
                    // Any other port -- the round number a loop writes -- is a place a value arrives
                    // in: copied as it is, with no tangent.
                    copyOf[ref] = draft.addPortNode(
                        node->dataType(), draft.allocateRuntimeSlot(node->dataType()));
                    copied[ref] = true;
                    ++copiedKinds;
                }
                break;
            }
            case rt::GCNodeKind::Data: {
                const std::vector<slot_t> slotValues(
                    function.staticSlots().begin(), function.staticSlots().end());
                const std::span<const slot_t> slots = slotValues;
                const std::ptrdiff_t index = -static_cast<std::ptrdiff_t>(node->dataIndex);
                if (node->dataIndex < 0 && index >= 0 &&
                    static_cast<size_t>(index) < slots.size()) {
                    copyOf[ref] = draft.materializeStaticValue(
                        slots[static_cast<size_t>(index)], node->dataType(),
                        rt::kGCNodeFlagConstant);
                    // A constant does not move: its tangent is the one zero every rule shares -- the zero
                    // of the constant's own type, since a tangent is a value of the value's space. A
                    // constant of a discrete type (a `bool` condition) has no tangent at all, and
                    // neither has one whose zero this transform does not write.
                    if (pairOf(node->dataType()) != nullptr) {
                        // An array constant's shape can be one its type leaves open (`float[]`), and
                        // then the value in the slot is what says how long the zero is.
                        const ::Array *shapeOf = nullptr;
                        if (node->dataType()->code() == camel::core::type::TypeCode::Array) {
                            shapeOf = camel::core::rtdata::fromSlot<const ::Array *>(
                                slots[static_cast<size_t>(index)]);
                        }
                        tangentOf[ref] = zeroConstantFor(node->dataType(), shapeOf);
                    }
                    copied[ref]    = true;
                    ++copiedKinds;
                }
                break;
            }
            case rt::GCNodeKind::Fill: {
                // A tuple the body builds. Its members are the values it puts in the tuple, in
                // order, and they go in as their *values*: the copy of a loop's state port is the
                // pair, and the value the tuple is made of is that pair's first half. The payload
                // travels as it is (the fill kind and the slot list name the same members), so a
                // Fill of an array or of a struct is copied as the thing it is.
                //
                // The *template* is the node's own Norm input, copied: a FILL takes every slot its
                // payload does not list from that template, so the template has to have the tuple's
                // arity and hold the same constants. `(x, 0.0)` lists one member and takes the second
                // slot from the template; a template of one field, as this walk used to make, makes
                // the fill read field 1 of a one-field tuple (measured: `fa`,
                // "Tuple field out of range: field 1 of a tuple with 1 fields").
                //
                // The tangent is the Fill of the components' tangents: the tangent of a tuple is the
                // tuple of the tangents of its parts (design principles 9.9), and that is the same
                // type exactly when every part is its own tangent space -- the only case in which
                // this transform writes a pair for the tuple at all. Every slot is listed in it: a
                // constant the value takes from the template has derivative *zero*, not the
                // constant, so the tangent cannot borrow the value's template.
                const std::span<const std::byte> payload = function.payloadOf(ref);
                // The payload *is* the fill body, slot list and all; a copy of the header alone
                // would leave slots() reading past its end (measured: it read node references).
                const auto *fill =
                    payload.size() >= sizeof(rt::GCFillBody)
                        ? reinterpret_cast<const rt::GCFillBody *>(payload.data())
                        : nullptr;
                const std::vector<gc_node_ref_t> members = function.withInputsOf(ref);
                const auto norm = function.normInputsOf(ref);
                std::vector<gc_node_ref_t> values;
                std::vector<gc_node_ref_t> memberTangents;
                values.reserve(members.size());
                memberTangents.reserve(members.size());
                bool allCopied = true;
                for (gc_node_ref_t member : members) {
                    if (member >= function.nodeCount() || !copied[member]) {
                        allCopied = false;
                        break;
                    }
                    values.push_back(
                        valueOf[member] != rt::kInvalidNodeRef ? valueOf[member] : copyOf[member]);
                    memberTangents.push_back(tangentOf[member]);
                }
                if (!allCopied) {
                    // A member the walk has not copied cannot be filled: the copy would name a node
                    // that is not there. Left uncopied, and the node that reads it refuses with the
                    // place, which is what a lift with no derivative for a shape owes its caller.
                    break;
                }
                const bool typeIsTuple =
                    node->dataType() != nullptr &&
                    node->dataType()->code() == camel::core::type::TypeCode::Tuple;
                const size_t arity =
                    typeIsTuple ? static_cast<TupleType *>(node->dataType())->size()
                                : members.size();
                const gc_node_ref_t templateCopy =
                    !norm.empty() && norm.front() < function.nodeCount() && copied[norm.front()]
                        ? copyOf[norm.front()]
                        : rt::kInvalidNodeRef;
                const gc_node_ref_t made = draft.addFillNode(node->dataType(), payload);
                const gc_node_ref_t fillTemplate =
                    templateCopy != rt::kInvalidNodeRef
                        ? templateCopy
                        : draft.materializeStaticValue(
                              camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(
                                  ::Tuple::create(arity, mm::autoSpace())),
                              node->dataType());
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(&fillTemplate, 1));
                if (!values.empty()) {
                    draft.setWithInputs(made, values);
                }
                copyOf[ref] = made;
                copied[ref] = true;
                ++copiedKinds;
                // A projection of this tuple is its member's half, not an index into a tuple built
                // for readers who want the whole thing (see `membersOfFill`).
                if (typeIsTuple) {
                    // By *slot*, not by position: a payload may list only some of the tuple's slots
                    // and take the rest from the template, and a slot the payload does not list is
                    // the template's constant, not the next listed member. A slot with no member
                    // stays invalid, so a projection of it is copied as the projection it is.
                    std::vector<gc_node_ref_t> bySlot(arity, rt::kInvalidNodeRef);
                    if (fill != nullptr) {
                        const std::span<const rt::gc_slot_idx_t> slots = fill->slots();
                        for (size_t i = 0; i < slots.size() && i < values.size(); ++i) {
                            if (slots[i] >= 0 && static_cast<size_t>(slots[i]) < arity) {
                                bySlot[static_cast<size_t>(slots[i])] = values[i];
                            }
                        }
                    }
                    membersOfFill[made] = std::move(bySlot);
                }
                if (pairOf(node->dataType()) != nullptr) {
                    // The components in slot order, with a zero for every slot the payload leaves to
                    // the template. A tuple's components are its fields, at their own types; an
                    // array's are its elements, all at the element type -- both are `Self` only when
                    // every component is, which is what `pairOf` answers, so the tangent is one fill
                    // of the same shape over the components' tangents (design principles 9.9).
                    const auto *tuple      = typeIsTuple ? static_cast<TupleType *>(node->dataType())
                                                         : nullptr;
                    const auto *array      = node->dataType() != nullptr &&
                                                     node->dataType()->code() ==
                                                         camel::core::type::TypeCode::Array
                                                 ? static_cast<ArrayType *>(node->dataType())
                                                 : nullptr;
                    const auto listed      = fill != nullptr
                                                 ? fill->slots()
                                                 : std::span<const rt::gc_slot_idx_t>{};
                    std::vector<gc_node_ref_t> components(arity, rt::kInvalidNodeRef);
                    bool complete = true;
                    for (size_t i = 0; i < listed.size() && i < memberTangents.size(); ++i) {
                        if (listed[i] < 0 || static_cast<size_t>(listed[i]) >= arity) {
                            complete = false;
                            break;
                        }
                        components[static_cast<size_t>(listed[i])] = memberTangents[i];
                        complete = complete && memberTangents[i] != rt::kInvalidNodeRef;
                    }
                    for (size_t s = 0; complete && s < arity; ++s) {
                        if (components[s] == rt::kInvalidNodeRef) {
                            components[s] = zeroConstantFor(
                                tuple != nullptr ? tuple->typeAt(s) : array->elemType());
                            complete      = components[s] != rt::kInvalidNodeRef;
                        }
                    }
                    if (complete) {
                        std::vector<size_t> slots(arity);
                        for (size_t s = 0; s < arity; ++s) {
                            slots[s] = s;
                        }
                        const std::vector<std::byte> tangentPayload = rt::makeFillPayload(
                            tuple != nullptr ? rt::GCFillKind::Tuple : rt::GCFillKind::Array,
                            slots);
                        tangentOf[ref] =
                            fillWithPayload(node->dataType(), tangentPayload, components);
                        if (tuple != nullptr) {
                            membersOfFill[tangentOf[ref]] = components;
                        }
                    }
                }
                break;
            }
            case rt::GCNodeKind::Accs: {
                // A projection out of an aggregate: `.0` of a tuple state, `.a` of a struct state.
                // Its value is the projection of the aggregate's value and its tangent is the
                // projection of the aggregate's tangent, at the same member -- the tangent of an
                // aggregate is the aggregate of its members' tangents (design principles 9.9), so the
                // j-th member's tangent is the j-th tangent. A tuple names its member by number and a
                // struct by key, and both are written as they are read: the draft has both forms
                // (`addAccsNode(type, tupleIndex)` and `addAccsNode(type, structKey)`), and reading a
                // keyed access by the number the payload carries would project another member.
                rt::GCAccsBody accsBody{};
                const auto *body    = function.bodyOf(ref, accsBody) ? &accsBody : nullptr;
                const auto inputs   = function.normInputsOf(ref);
                const bool tupleIndexed =
                    body != nullptr && body->accsKind == rt::GCAccsKind::TupleIndex;
                const bool keyed = body != nullptr && body->accsKind == rt::GCAccsKind::StructKey;
                if ((!tupleIndexed && !keyed) || inputs.size() != 1 ||
                    inputs.front() >= function.nodeCount() || !copied[inputs.front()]) {
                    break;
                }
                const auto project = [&](gc_node_ref_t from) {
                    const gc_node_ref_t projection =
                        keyed ? draft.addAccsNode(node->dataType(), body->key())
                              : draft.addAccsNode(node->dataType(), body->value);
                    draft.setNormInputs(projection, std::span<const gc_node_ref_t>(&from, 1));
                    return projection;
                };
                const gc_node_ref_t tuple = inputs.front();
                const gc_node_ref_t source =
                    valueOf[tuple] != rt::kInvalidNodeRef ? valueOf[tuple] : copyOf[tuple];
                // A member of a tuple this transform filled is that member's own node, not a projection of the
                // tuple: the fill exists so a reader of the *whole* tuple sees what the program sees, and
                // a reader of one member has no business reading every member (and, for a lifted loop's
                // value tuple, reaching the loop's other component -- measured 2026-11-03,
                // `(loop t = (x, x) ...).1`).
                const auto memberHalf = [&](gc_node_ref_t from) -> gc_node_ref_t {
                    const auto at = tupleIndexed ? membersOfFill.find(from) : membersOfFill.end();
                    if (at == membersOfFill.end() || body->value >= at->second.size()) {
                        return rt::kInvalidNodeRef;
                    }
                    return at->second[body->value];
                };
                const gc_node_ref_t valueMember = memberHalf(source);
                copyOf[ref]                     = valueMember != rt::kInvalidNodeRef ? valueMember
                                                                                    : project(source);
                copied[ref] = true;
                ++copiedKinds;
                if (tangentOf[tuple] != rt::kInvalidNodeRef) {
                    const gc_node_ref_t tangentMember = memberHalf(tangentOf[tuple]);
                    tangentOf[ref]                   = tangentMember != rt::kInvalidNodeRef
                                                          ? tangentMember
                                                          : project(tangentOf[tuple]);
                }
                break;
            }
            case rt::GCNodeKind::Join: {
                // Made before the walk (its type is the pair of the value it yields). Its inputs are
                // wired after it.
                const auto at = liftedJoin.find(ref);
                if (at == liftedJoin.end()) {
                    complete = false;
                    break;
                }
                copyOf[ref] = at->second;
                copied[ref] = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Brch: {
                // The condition is wired after the walk, because it is a value the walk has not
                // reached yet. What the branch does not change is the condition itself: it is
                // copied as written and read by the Brch, never differentiated -- the choice of arm
                // is not a value the derivative flows through.
                rt::GCBrchBody brchBody{};
            const auto *body = function.bodyOf(ref, brchBody) ? &brchBody : nullptr;
                const auto at    = joinOfBrch.find(ref);
                if (body == nullptr || at == joinOfBrch.end()) {
                    complete = false;
                    break;
                }
                copyOf[ref] = draft.addBrchNode(Type::Int64(), at->second, body->armCount);
                copied[ref] = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Func: {
                // A `Func` node names its callee graph, and it is one of two things: a function
                // *value* (a type of the function code, carried as it is -- nothing differentiates
                // through a value) or the call itself (the type is the callee's result and the norm
                // inputs are the arguments). A call is lifted as a call: the lifted callee takes
                // pairs and answers with one, so the arguments go in as `(value, tangent)` and the
                // readers of the result project the pair.
                const auto at = liftedCalleeOf.find(ref);
                if (at == liftedCalleeOf.end()) {
                    return fail(refusal);
                }
                camel::runtime::GCGraph *callee = program->graph(function.calleeIdOf(ref));
                if (callee == nullptr) {
                    return fail(refusal);
                }
                const bool isValue = node->dataType() != nullptr &&
                                     node->dataType()->code() == camel::core::type::TypeCode::Function;
                // A **call** is filled in against the callee's own lifted signature -- the one its
                // lift declared, whose `with` values are promoted into the ordinary list
                // (`liftSignature`) -- so that signature is what says how long the value half is.
                // A function **value** keeps the declared `Jvp<F>`: a value is carried, not called,
                // and the type a body sees for it is the type the declaration writes.
                FunctionType *calleeLifted = nullptr;
                FunctionType *calleeSignature = nullptr;
                if (isValue) {
                    calleeSignature = jvpSignature(callee->funcType());
                    if (calleeSignature == nullptr) {
                        return fail(refusal);
                    }
                } else {
                    const auto declared =
                        state.calleeTypes.find(rt::toValue(function.calleeIdOf(ref)));
                    calleeLifted = declared != state.calleeTypes.end()
                                       ? declared->second
                                       : jvpCalleeSignature(callee->funcType());
                    if (calleeLifted == nullptr) {
                        return fail(refusal);
                    }
                }
                // A callee that takes no value of its own -- every value it reads arrives through a
                // `with` parameter -- has no value half that is not a promoted `with` value. Met
                // inside a **callee's** lift, the cut cuts that callee a second time and the halves it
                // makes name a graph the nested program does not carry: `nodevm.cpp`'s
                // `runtimeTarget != nullptr` ("a direct call names a function whose graph is not in
                // the program's graph table", measured 2026-11-04 on `grad_first_with_parameter`'s
                // `sx`, whose `scaled_x` calls `scaled`: two `with` parameters and no parameter of
                // its own). One level is fine -- `sk` calls the same `scaled` from the top-level lift
                // and answers 6.0 -- so the gate is the *second* level of pair-answering calls under a
                // promoted `with` value, and it refuses by name until `split` adopts that graph rather
                // than letting the run time die on a call with no target.
                if (state.promoteWiths && callee->funcType() != nullptr &&
                    callee->funcType()->normTypesCount() == 0 &&
                    callee->funcType()->withTypesCount() > 0) {
                    JvpGraph refused;
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(
                        function.nodeOrigin(ref));
                    refused.refusedConstruct = std::format(
                        "a call to `{}`, which takes nothing but its `with` values",
                        callee->name());
                    refused.refusedWhy =
                        "this lift is itself a callee's, so the cut cuts the callee it calls a "
                        "second time, and the halves of a function whose whole value half is "
                        "promoted `with` values are a graph the nested program does not hold. One "
                        "level of that call is built and answers its numbers; the second is refused "
                        "here until the cut adopts the graph it names.";
                    os << "REINTERP jvp function=" << name
                       << " skip=" << refusalCauseName(RefusalCause::Callee)
                       << " callee=" << callee->name() << '\n';
                    noteRefusal(
                        RefusalReading("jvp", name, RefusalCause::Callee).atCallee(callee->name()));
                    return fail(refusal, refused);
                }
                // The arguments, in the order the lifted callee declares them: **every value first**
                // -- the ordinary parameters and then the `with` values this lift promoted into the
                // ordinary list (`liftSignature`) -- and **their tangents after them**, in the same
                // order. A call that pushed each value with its tangent as it walked the inputs
                // passed `(v1, d1, v2, d2)` to a callee whose list is `(v1, v2, d1, d2)`: right for
                // one parameter by accident, and refused for two (`split` reads the seam at
                // `count / 2` and found a tangent where it wanted a value -- measured 2026-11-04 on
                // `vjp((rr) => mulxy(rr, 2.0))(3.0)`, "the value half reads the tangent of parameter
                // 0, which the forward half does not take").
                //
                // The value half's length is the callee's own answer, not this graph's guess: it is
                // `calleeLifted`'s first half, and a call whose inputs do not fill exactly one value
                // per slot and one tangent per slot is refused rather than misaligned -- a call the
                // run time checks by count would otherwise land every argument one place off.
                const size_t valueSlots =
                    calleeLifted != nullptr ? calleeLifted->normTypesCount() / 2 : 0;
                std::vector<gc_node_ref_t> values;
                std::vector<gc_node_ref_t> tangents;
                // The tangent of a value with no tangent space is the `()` slot the lifted signature
                // writes for it: the slot is a port there, so the call has to fill it, and a `()`
                // value is the only thing that fits. One node, shared: it is a constant.
                gc_node_ref_t emptyTangent = rt::kInvalidNodeRef;
                const auto tangentSlotOf = [&](gc_node_ref_t input, const NodeFace *in) {
                    if (in != nullptr && pairOf(in->dataType()) != nullptr) {
                        if (tangentOf[input] == rt::kInvalidNodeRef) {
                            return rt::kInvalidNodeRef;
                        }
                        return tangentOf[input];
                    }
                    if (emptyTangent == rt::kInvalidNodeRef) {
                        emptyTangent = draft.materializeStaticValue(
                            camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(
                                ::Tuple::create(0, mm::autoSpace())),
                            emptyTupleType());
                    }
                    return emptyTangent;
                };
                std::vector<gc_node_ref_t> carries;
                std::vector<gc_node_ref_t> arguments;
                if (!isValue) {
                    for (gc_node_ref_t input : function.normInputsOf(ref)) {
                        if (input >= function.nodeCount() || !copied[input]) {
                            return fail(refusal);
                        }
                        const NodeFace *in = function.node(input);
                        const gc_node_ref_t half =
                            valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input];
                        // The lifted callee takes the value and its tangent as two ordinary
                        // arguments (the signature is `(T, T) => ...`), not as one tuple -- and the
                        // tangent goes into the second half of the list, so the two are collected
                        // apart and joined below.
                        values.push_back(half);
                        const gc_node_ref_t tangent = tangentSlotOf(input, in);
                        if (tangent == rt::kInvalidNodeRef) {
                            return fail(refusal);
                        }
                        tangents.push_back(tangent);
                    }
                    const std::vector<gc_node_ref_t> withArgs = function.withInputsOf(ref);
                    for (size_t i = 0; i < withArgs.size(); ++i) {
                        const gc_node_ref_t input = withArgs[i];
                        if (input >= function.nodeCount() || !copied[input]) {
                            return fail(refusal);
                        }
                        const NodeFace *in = function.node(input);
                        const gc_node_ref_t half =
                            valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input];
                        // A `with` argument the callee's lift promoted is a value of its ordinary
                        // list, and the direction it goes with is the tangent this graph already
                        // carries for it -- which is the whole of the fix: `weighted`'s lifted call
                        // is handed the value of `ww` *and* the direction `dww`, so its `lin` is not
                        // constant in the weight. One that stayed a `with` parameter (a `var` cell)
                        // is passed as it stands, value only -- the same two sides of one question
                        // `liftSignature` answers, asked here about the callee's own declaration.
                        if (i < callee->funcType()->withTypesCount() &&
                            withIsPromoted(callee->funcType(), i)) {
                            values.push_back(half);
                            const gc_node_ref_t tangent = tangentSlotOf(input, in);
                            if (tangent == rt::kInvalidNodeRef) {
                                return fail(refusal);
                            }
                            tangents.push_back(tangent);
                        } else {
                            carries.push_back(half);
                        }
                    }
                    if (values.size() != valueSlots) {
                        // The call's inputs do not fill the callee's value half one slot each. A call
                        // the run time checks by count would take every argument one place off, so
                        // this is refused rather than written: the two lists have to be the same
                        // list, and the callee's own signature is what says how long it is.
                        return fail(refusal);
                    }
                    arguments.reserve(values.size() + tangents.size());
                    arguments.insert(arguments.end(), values.begin(), values.end());
                    arguments.insert(arguments.end(), tangents.begin(), tangents.end());
                }
                TupleType *resultPair = pairOf(node->dataType());
                if (!isValue && resultPair == nullptr) {
                    // A call whose answer this transform carries no pair for: the lifted callee would
                    // answer one, so the call cannot be copied either. Refused, not guessed.
                    return fail(refusal);
                }
                camel::core::type::Type *valueType =
                    isValue ? static_cast<camel::core::type::Type *>(calleeSignature)
                            : static_cast<camel::core::type::Type *>(resultPair);
                const gc_node_ref_t made = draft.addFuncNode(
                    at->second, valueType, 0, draft.allocateRuntimeSlot(valueType), 0);
                if (!isValue) {
                    if (!arguments.empty()) {
                        draft.setNormInputs(made, arguments);
                    }
                    if (!carries.empty()) {
                        draft.setWithInputs(made, carries);
                    }
                }
                copyOf[ref] = made;
                if (!isValue) {
                    const gc_node_ref_t half = draft.addAccsNode(node->dataType(), 0);
                    draft.setNormInputs(half, std::span<const gc_node_ref_t>(&made, 1));
                    const gc_node_ref_t halfTangent = draft.addAccsNode(node->dataType(), 1);
                    draft.setNormInputs(halfTangent, std::span<const gc_node_ref_t>(&made, 1));
                    valueOf[ref]   = half;
                    tangentOf[ref] = halfTangent;
                    pairIsTheCopy.insert(ref);
                }
                copied[ref] = true;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Call: {
                // A `Call` node is a call **through a value**: its first norm input is the function
                // value, and which graph that value stands for is a question only the run time
                // answers. This transform's answer is a graph, and a lifted call has to reach the
                // *lifted* callee -- the arguments go in as `(value, tangent)` and the answer comes
                // back as a pair -- so a call whose callee is not a graph this lift can name has no
                // lifted callee to reach. The lift used to copy the call as it stood and pair its
                // arguments anyway, and the answer was a process that died where the number should
                // have been: `jvp(use)(plain, 2.0, (), 1.0)` on
                // `func use(f: (y: float) => float, z: float): float { return f(z) }` segfaults on
                // `std::nvm`, `std::fvm` and `std::jit` (`SIGSEGV` in `NodeVMSchedPass::call` ->
                // `rtdata::isViewValue`, measured 2026-11-05), and so does a body that calls a
                // function value it made for itself. A crash is worse than a refusal (design
                // principles 9.9: a rule that is missing is refused at the construct, never answered
                // with a wrong value), so the construct is named here instead.
                //
                // Refusing is also what the engine says about the same shape -- its pullback refuses
                // `cannot differentiate through this call of a function value: which function it
                // calls is not evident` -- and that is why a library transform written as a
                // composition cannot wrap its subject in a body yet: see `stdlib/vjp.cml`.
                JvpGraph refused;
                refused.refusedOrigin =
                    static_cast<camel::source::origin_id_t>(function.nodeOrigin(ref));
                refused.refusedConstruct = "a call through a function value";
                refused.refusedWhy =
                    "the callee is a value the body reads, so the lift has no graph of its own to "
                    "lift for this call: a lifted call reaches a lifted callee and passes it "
                    "(value, tangent) pairs, and a value whose graph is only known when the program "
                    "runs has no lifted callee to reach";
                os << "REINTERP jvp function=" << name
                   << " skip=" << refusalCauseName(RefusalCause::Callee)
                   << " construct=" << refused.refusedConstruct << '\n';
                noteRefusal(RefusalReading("jvp", name, RefusalCause::Callee)
                                .atConstruct(refused.refusedConstruct));
                return fail(refusal, refused);
            }
            case rt::GCNodeKind::Loop: {
                // A loop is a region of this graph: the rounds are the region's and the LOOP
                // node's value is the state. Lifting it is lifting the state into a pair -- the
                // node's type is the pair, the body's state port is a pair port (whose halves the
                // body reads through projections), and the region's result is the pair of the
                // body's new value and its tangent. The inputs and the region are wired after the
                // walk. The node names no graph: the rounds are the region (design principles 3.10).
                //
                // A loop a producer gave collection slots has a value that is not the state alone:
                // it is `Tuple(state…, array…)` (`LoopView`), and only the state part is lifted.
                // `liftedLoopValueType` answers both cases, and the body's result is written by the
                // same rule, so the node's type and what its region yields say the same thing.
                rt::GCLoopBody body{};
                function.bodyOf(ref, body);
                rt::GCLoopBody liftedBody = body;
                const size_t stateCount   = loopStateCountOf(ref, body);
                Type *liftedValueType =
                    liftedLoopValueType(node->dataType(), body.collectionCount, stateCount);

                if (liftedValueType == nullptr) {
                    // A loop whose state this transform cannot pair: the copy would have the old
                    // value type and a body yielding the pair, and the two would only disagree at run
                    // time. Refused here instead (design principles: a wrong derivative that looks
                    // like one is the expensive kind of wrong).
                    complete = false;
                    break;
                }
                copyOf[ref] = draft.addLoopNode(liftedBody, liftedValueType);
                liftedLoopNode = copyOf[ref];
                copied[ref]    = true;
                ++copiedKinds;
                // What the node hands its readers. With one state the lifted value *is* that state's
                // pair, so a reader projects the two halves exactly as it does from a lifted call.
                // With more, the value is the tuple of the states' pairs -- component j is
                // `(state_j, its tangent)` -- and a reader of one state (`t.0` of the value) or of the
                // whole thing at an exit wants the two halves back in tuple form: the value tuple and
                // the tangent tuple, filled from the components' halves. A loop that keeps collection
                // slots is left alone: its value carries the tapes, and no reader wants half of a
                // recording.
                if (body.collectionCount == 0 && pairOf(node->dataType()) != nullptr) {
                    if (stateCount == 1) {
                        const gc_node_ref_t loopPair = copyOf[ref];
                        const gc_node_ref_t half     = draft.addAccsNode(node->dataType(), 0);
                        draft.setNormInputs(half, std::span<const gc_node_ref_t>(&loopPair, 1));
                        const gc_node_ref_t halfTangent =
                            draft.addAccsNode(node->dataType(), 1);
                        draft.setNormInputs(
                            halfTangent, std::span<const gc_node_ref_t>(&loopPair, 1));
                        valueOf[ref]   = half;
                        tangentOf[ref] = halfTangent;
                        pairIsTheCopy.insert(ref);
                    } else if (liftedValueType->code() == camel::core::type::TypeCode::Tuple) {
                        const auto *valueTuple = static_cast<TupleType *>(liftedValueType);
                        const size_t count     = valueTuple->size();
                        std::vector<gc_node_ref_t> values;
                        std::vector<gc_node_ref_t> tangents;
                        std::vector<size_t> slots(count);
                        values.reserve(count);
                        tangents.reserve(count);
                        for (size_t j = 0; j < count; ++j) {
                            slots[j]                      = j;
                            const gc_node_ref_t loopValue = copyOf[ref];
                            const gc_node_ref_t component =
                                draft.addAccsNode(valueTuple->typeAt(j), static_cast<uint32_t>(j));
                            draft.setNormInputs(
                                component, std::span<const gc_node_ref_t>(&loopValue, 1));
                            const auto *pairType =
                                static_cast<const TupleType *>(valueTuple->typeAt(j));
                            const gc_node_ref_t half = draft.addAccsNode(pairType->typeAt(0), 0);
                            draft.setNormInputs(half,
                                                std::span<const gc_node_ref_t>(&component, 1));
                            const gc_node_ref_t halfTangent =
                                draft.addAccsNode(pairType->typeAt(1), 1);
                            draft.setNormInputs(
                                halfTangent, std::span<const gc_node_ref_t>(&component, 1));
                            values.push_back(half);
                            tangents.push_back(halfTangent);
                        }
                        const std::vector<std::byte> payload =
                            rt::makeFillPayload(rt::GCFillKind::Tuple, slots);
                        valueOf[ref]   = fillWithPayload(node->dataType(), payload, values);
                        tangentOf[ref] = fillWithPayload(node->dataType(), payload, tangents);
                        // A reader of one member answers that member's half; see `membersOfFill`.
                        membersOfFill[valueOf[ref]]   = values;
                        membersOfFill[tangentOf[ref]] = tangents;
                    }
                }
                break;
            }
            case rt::GCNodeKind::Oper: {
                camel::runtime::GCOperBody operBody{};
            const auto *body = function.bodyOf(ref, operBody) ? &operBody : nullptr;
                const gc_node_ref_t made =
                    draft.addOperNode(node->dataType(), nullptr, body->uri());
                std::vector<gc_node_ref_t> originals;
                std::vector<gc_node_ref_t> inputs;
                for (gc_node_ref_t input : function.normInputsOf(ref)) {
                    // A primitive whose input was not copied cannot be reproduced: the copy would
                    // name a node that is not there, and the transform would answer with a graph
                    // that computes something else. The build says so and gives nothing.
                    if (input >= function.nodeCount() || !copied[input]) {
                        complete = false;
                        break;
                    }
                    originals.push_back(input);
                    inputs.push_back(valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input]);
                }
                // Where the data inputs end: everything appended past this is a `<>` argument, a
                // static slot of the operator rather than a value the node passes as data. The list
                // stays one list in port order; this number is what tells the two roles apart when a
                // rule's parameters are lined up with it (`portsTakeOperands`).
                const size_t dataOperandCount = originals.size();
                copyOf[ref] = made;
                copied[ref] = true;
                ++copiedKinds;
                if (!complete) {
                    break;
                }
                if (!inputs.empty()) {
                    draft.setNormInputs(made, inputs);
                }
                // The operator's `<>` arguments are ports of the same node, in the same order and
                // after the normal ones (`OpDef::params` written `withAfterNorm`; `dataPortsOf` and
                // `portRoleOf` in camel/runtime/draft.h are that one list). A rule's parameters
                // follow the port order too -- `modules/transforms/vmap.cpp` states the protocol
                // where it builds a call, "a `with` argument is a rule's argument too" -- so a copy
                // that dropped them would be a *different operator*: `softmax<1>(x)` copied without
                // its axis is `softmax(x)`, which reads the last axis, and `mean<0, true>(x)` copied
                // without its two is a mean with the default axis and no kept dimension. That is a
                // wrong number, not a missing one, so they are copied and attached as the normal
                // ones are. They are appended to `originals`/`inputs` so that everything below --
                // the cotangents the rule is handed, the types it is asked about, and the values it
                // reads -- sees one port list in port order.
                std::vector<gc_node_ref_t> contextInputs;
                for (gc_node_ref_t input : function.withInputsOf(ref)) {
                    if (input >= function.nodeCount() || !copied[input]) {
                        complete = false;
                        break;
                    }
                    originals.push_back(input);
                    contextInputs.push_back(
                        valueOf[input] != rt::kInvalidNodeRef ? valueOf[input] : copyOf[input]);
                    inputs.push_back(contextInputs.back());
                }
                if (!complete) {
                    break;
                }
                if (!contextInputs.empty()) {
                    draft.setWithInputs(made, contextInputs);
                }
                // The `sync` block's chain, copied edge for edge: a statement is executed because it
                // sits on that chain, and an effect whose result nobody reads is dropped at run time
                // without it. The flag says the node is one of the block's statements; the waits are
                // the statements written before it.
                {
                    std::vector<gc_node_ref_t> waits;
                    for (gc_node_ref_t edge : function.syncInputsOf(ref)) {
                        if (edge < function.nodeCount() && copied[edge]) {
                            waits.push_back(copyOf[edge]);
                        }
                    }
                    if (!waits.empty()) {
                        draft.setSyncInputs(made, waits);
                    }
                    // The flag is the original's own: a statement the block wrote, whether or not
                    // anything was written before it (the first statement has no waits and is still
                    // a statement).
                    if (node->hasFlag(rt::kGCNodeFlagSyncStatement)) {
                        draft.setNodeRuntimeFlags(
                            made, static_cast<uint8_t>(rt::kGCNodeFlagSyncStatement));
                    }
                }
                if (!camel::core::tangentSpaceOf(node->dataType()).hasSpace()) {
                    // A discrete value -- a comparison's `bool`, a count's `int` -- has no tangent:
                    // it is copied so the nodes that read it have their operand, and no rule is
                    // asked to produce a derivative for it (design principles 9.9: a discrete
                    // value's derivative is zero by definition, and a zero nobody reads is not
                    // worth a node). Whether a value has a tangent at all is `tangentSpaceOf`'s
                    // answer, not a comparison with `float`: an array of floats is its own tangent
                    // space, and it is walked like a float -- by the rule filed under its operator.
                    break;
                }
                const std::string_view uri = body->uri();
                // An operator with an external effect (a print, the clock, the profiler) is
                // executed as written, and its derivative is zero: asking the rule table for a
                // forward rule would ask it to differentiate a write to the outside world, and the
                // honest answer -- "there is no such rule" -- would stop a program whose effect
                // the lift has no business touching. The copy keeps the operator and its inputs, so
                // the effect runs the same number of times it ran before.
                if (camel::core::OperatorTable::instance().hasExternalEffect(uri)) {
                    if (pairOf(node->dataType()) != nullptr) {
                        tangentOf[ref] = zeroConstantFor(node->dataType());
                    }
                    break;
                }
                std::vector<gc_node_ref_t> tangents;
                for (gc_node_ref_t original : originals) {
                    // An operand with no tangent space -- the `i` of `xs[i]`, a branch's condition --
                    // contributes no slot: the rule is handed the tangents of the operands that have
                    // one, in their own order (design principles 9.9, and the same "no slot" the
                    // `Tangent<bool> = ()` reading uses -- a slot that does not exist, not a zero
                    // that does).
                    const NodeFace *input = function.node(original);
                    if (input != nullptr && !camel::core::tangentSpaceOf(input->dataType()).hasSpace()) {
                        continue;
                    }
                    if (tangentOf[original] == rt::kInvalidNodeRef) {
                        complete = false;
                        break;
                    }
                    tangents.push_back(tangentOf[original]);
                }
                if (!complete) {
                    break;
                }
                std::string ruleName;
                std::string ruleWhy;
                // The tangents whose zero is a fact about this graph rather than a number a program
                // produced: the ones `zeroConstantFor` wrote for a value that cannot move, and a
                // constant zero a law of its own wrote. The rule's right side is expanded with them
                // named, so the term one of them multiplies is omitted instead of computed
                // (`rule_expand.cpp#expandRuleRightSide`, author's ruling of 2026-11-04).
                std::unordered_set<gc_node_ref_t> structurallyZero;
                for (const gc_node_ref_t tangent : tangents) {
                    if (isStructuralZeroTangent(draft, zeroMade, tangent)) {
                        structurallyZero.insert(tangent);
                    }
                }
                std::vector<std::string> omitted;
                // The types the operator's ports have, in the order the rule is handed them: what a
                // candidate's ports have to match before anything is built. The list is the node's
                // ports -- the data inputs, then the `<>` ones -- and `dataOperandCount` is kept so
                // that the comparison can tell a value the node passes from a slot its own left side
                // writes. The rule is asked for at the *element* type of the operand
                // (`rule_types.h`), and that answer is about the element, not about the shape:
                // `:op/at od` takes a `float[]` and the scalar law `jvp_dtod` is an instance at
                // `float`, so a candidate whose ports say `float` may be offered operands that are
                // `float[]`.
                std::vector<camel::core::type::Type *> operandTypes;
                for (gc_node_ref_t original : originals) {
                    const NodeFace *input = function.node(original);
                    operandTypes.push_back(input != nullptr ? input->dataType() : nullptr);
                }
                // The rule is generic in the element of the operand it was matched on: `+` over
                // `f32` is a rule at `f32`, and `sum_axis` over a `float[]` is one at `float`
                // (the array's rows are the batch, not the type of the rule). `rule_types.h` is
                // that answer, and it is the same one `vmap` asks for.
                Type *ruleElementType = camel::transforms::rules::ruleElementTypeOf(
                    camel::transforms::rules::firstOperandTypeOf(function, ref));
                tangentOf[ref] =
                    buildRuleTangent(
                        *context, state.group, transaction, uri, inputs, tangents, ruleElementType,
                        operandTypes, dataOperandCount, ruleName, ruleWhy, &structurallyZero,
                        &omitted);
                if (!omitted.empty()) {
                    // The reading of the omission, in the rule's own words: these are the places the
                    // expansion did *not* copy, because they fed only a product whose tangent is
                    // structurally zero. A case that pins this line goes red the day the term is
                    // computed again -- it names absence, not a number that happens to be zero.
                    std::string uris;
                    for (const std::string &uri : omitted) {
                        uris += (uris.empty() ? "" : ",") + uri;
                    }
                    os << "REINTERP jvp rule=" << ruleName
                       << " omit=structurally-zero-tangent nodes=" << omitted.size()
                       << " uris=" << uris << '\n';
                }
                if (tangentOf[ref] == rt::kInvalidNodeRef) {
                    // Named, not guessed: the transform has no derivative for this operator, and
                    // the refusal carries the place the primitive was written -- the node's origin,
                    // which the kernel turns into a fault at that `file:line` -- so the error names
                    // the operator in `f`, not the call that asked for `jvp`.
                    os << "REINTERP jvp function=" << name
                       << " skip=" << refusalCauseName(RefusalCause::Rule) << " uri=" << uri
                       << " reason=" << ruleWhy << '\n';
                    noteRefusal(RefusalReading("jvp", name, RefusalCause::Rule).atUri(uri));
                    JvpGraph refused;
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(
                        function.nodeOrigin(ref));
                    refused.refusedUri = std::string(uri);
                    refused.refusedWhy = ruleWhy;
                    return fail(refusal, refused);
                }
                if (std::find(usedRules.begin(), usedRules.end(), ruleName) == usedRules.end()) {
                    usedRules.push_back(ruleName);
                }
                break;
            }
            default:
                break;
            }
        }
        if (!complete) {
            os << "REINTERP jvp function=" << name
               << " skip=" << refusalCauseName(RefusalCause::Body) << '\n';
            noteRefusal(RefusalReading("jvp", name, RefusalCause::Body));
            return fail(refusal);
        }
        draft.setInsertRegion(0);
        // The wiring the walk deferred. Each arm yields the lifted pair of its value and its
        // tangent, the Join names the Brch and those pairs, and the Brch names its condition and
        // its Join. Which arm is which, and what each arm yields, is read from the original graph;
        // nothing about branches is decided a second time here.
        for (gc_node_ref_t ref = 0; complete && ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Brch) {
                continue;
            }
            rt::GCBrchBody brchBody{};
            const auto *body     = function.bodyOf(ref, brchBody) ? &brchBody : nullptr;
            const gc_node_ref_t brch = copyOf[ref];
            if (body == nullptr || brch == rt::kInvalidNodeRef) {
                complete = false;
                break;
            }
            std::vector<gc_node_ref_t> condition;
            for (gc_node_ref_t input : function.normInputsOf(ref)) {
                if (input >= function.nodeCount() || !copied[input]) {
                    complete = false;
                    break;
                }
                condition.push_back(copyOf[input]);
            }
            if (!complete) {
                break;
            }
            if (!condition.empty()) {
                draft.setNormInputs(brch, condition);
            }
            std::vector<gc_node_ref_t> armResults;
            for (rt::gc_cnt_t k = 0; k < body->armCount; ++k) {
                const rt::GCRegion *arm = nullptr;
                uint32_t armRegion      = 0;
                for (uint32_t r = 1; r < function.regionCount(); ++r) {
                    const rt::GCRegion *candidate = function.region(r);
                    if (candidate != nullptr && candidate->kind == rt::GCRegionKind::Arm &&
                        candidate->owner == ref && candidate->index == static_cast<uint32_t>(k)) {
                        arm       = candidate;
                        armRegion = r;
                        break;
                    }
                }
                if (arm == nullptr || arm->result == rt::kInvalidNodeRef ||
                    !copied[arm->result] || tangentOf[arm->result] == rt::kInvalidNodeRef) {
                    complete = false;
                    break;
                }
                draft.setInsertRegion(liftedRegion[armRegion]);
                const gc_node_ref_t pair = liftedPair(
                    valueTypeOf(arm->result), copyOf[arm->result], tangentOf[arm->result]);
                draft.setRegionOwner(liftedRegion[armRegion], brch, static_cast<uint32_t>(k));
                draft.setRegionResult(liftedRegion[armRegion], pair);
                draft.setInsertRegion(0);
                armResults.push_back(pair);
            }
            if (!complete) {
                break;
            }
            const gc_node_ref_t join = joinOfBrch.at(ref);
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
            const rt::GCJoinBody joinBody{.brch = brch, .armCount = body->armCount};
            init.payload = std::span<const std::byte>(
                reinterpret_cast<const std::byte *>(&joinBody), sizeof(joinBody));
            init.normInputs = std::span<const gc_node_ref_t>(&brch, 1);
            init.withInputs = armResults;
            draft.rewriteNode(join, init);
        }
        if (!complete) {
            os << "REINTERP jvp function=" << name
               << " skip=" << refusalCauseName(RefusalCause::Branch) << '\n';
            noteRefusal(RefusalReading("jvp", name, RefusalCause::Branch));
            return fail(refusal);
        }
        // A loop's wiring: its state input becomes a pair (the initial state and its zero tangent),
        // its body region keeps its ports -- the state port now a pair -- and its result becomes the
        // pair the body computes. The loop's value is then a lifted value like any other, so a
        // reader of it needs nothing special, including the body's exit.
        for (gc_node_ref_t ref = 0; complete && ref < function.nodeCount(); ++ref) {
            const NodeFace *node = function.node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Loop) {
                continue;
            }
            const gc_node_ref_t loopCopy = copyOf[ref];
            if (loopCopy == rt::kInvalidNodeRef) {
                complete = false;
                break;
            }
            // How many values this loop collects into arrays each round: the shape of its value and
            // of its body's result (`LoopView::collectionCount`). Read here as well as in the walk,
            // from the one node, so the two readings are the same reading.
            rt::GCLoopBody body{};
            function.bodyOf(ref, body);
            const size_t stateCount = loopStateCountOf(ref, body);
            // The body region comes first: its result is the pair the state becomes, and the loop
            // node's *first with-input* is that same result (LoopView::bodyResult reads it there),
            // so it has to be the pair and not the copy of the old exit.
            gc_node_ref_t newState = rt::kInvalidNodeRef;
            for (uint32_t r = 1; r < function.regionCount(); ++r) {
                const rt::GCRegion *part = function.region(r);
                if (part == nullptr || part->owner != ref ||
                    part->kind != rt::GCRegionKind::LoopBody) {
                    continue;
                }
                draft.setRegionOwner(liftedRegion[r], loopCopy, part->index);
                std::vector<gc_node_ref_t> ports;
                for (gc_node_ref_t port : function.regionPorts(r)) {
                    if (port >= function.nodeCount() || !copied[port]) {
                        complete = false;
                        break;
                    }
                    ports.push_back(copyOf[port]);
                }
                if (!complete) {
                    break;
                }
                if (!ports.empty()) {
                    draft.setRegionPorts(liftedRegion[r], ports);
                }
                const gc_node_ref_t exit = part->result;
                if (exit == rt::kInvalidNodeRef || !copied[exit]) {
                    complete = false;
                    break;
                }
                draft.setInsertRegion(liftedRegion[r]);
                if (body.collectionCount == 0 && stateCount == 1) {
                    // A loop that collects nothing and carries one state yields that state and
                    // nothing else, so its body's result *is* the value: the exit becomes the pair of
                    // it and its tangent, which is the path this lift has always taken.
                    if (tangentOf[exit] == rt::kInvalidNodeRef) {
                        complete = false;
                        break;
                    }
                    newState = liftedPair(valueTypeOf(exit), copyOf[exit], tangentOf[exit]);
                } else {
                    // More than one state, or a tape: the body yields `Tuple(next state…, value per
                    // collection slot)`, the state part is lifted component by component and the
                    // collection parts are spliced back as they are -- the same split the loop's own
                    // value type went through (`liftedLoopValueType`). Both halves are written, or
                    // neither is: a body yielding the pairs while the node's type still says
                    // `Tuple(state, array)` is a graph that builds and fails only at run time.
                    newState = liftedLoopBodyResult(exit, body.collectionCount, stateCount);
                }

                if (newState == rt::kInvalidNodeRef) {
                    complete = false;
                    break;
                }
                draft.setRegionResult(liftedRegion[r], newState);
                draft.setInsertRegion(0);
            }
            if (!complete || newState == rt::kInvalidNodeRef) {
                complete = false;
                break;
            }
            std::vector<gc_node_ref_t> loopInputs;
            // The inputs are the state values and, before them, the array a `for` loop walks
            // (`LoopView::iterable` and `LoopView::initialState` name the two). The array is not a
            // state: the loop still iterates the values it was given, and no derivative flows
            // through the array itself. Pairing it would hand the loop a pair where it asks for an
            // array -- and `pairOf` pairs it once an array of floats answers `Self`, which is why
            // this one input is named instead of being left to the type. An array that *does*
            // carry a tangent is refused where it is read (below), because the element's tangent
            // has no place in this shape and answering its zero is a wrong number.
            size_t inputIndex = 0;
            const size_t stateStart = camel::core::loopHasIterable(body.kind) ? 1 : 0;
            for (gc_node_ref_t input : function.normInputsOf(ref)) {
                if (input >= function.nodeCount() || !copied[input]) {
                    complete = false;
                    break;
                }
                const NodeFace *in = function.node(input);
                const gc_node_ref_t copy         = copyOf[input];
                if (inputIndex++ < stateStart) {
                    // The array a `for` loop walks (`LoopView::iterable`). It is copied as it
                    // stands, so the element the body is handed has no tangent -- right for a
                    // sequence nobody differentiates (`[1.0, 10.0]`, whose tangent this transform
                    // writes as the zero of its space) and wrong for one that carries a tangent,
                    // where the element's tangent is the array's element: `jvp(f)` of
                    // `f(xs) = loop s = 0.0 for e in xs do s + e` at `xs = [1.0, 2.0]` with
                    // `dxs = [1.0, 1.0]` answered the value 3.0 and the tangent 0.0, where the sum
                    // of the tangents is 2.0 (measured 2026-11-04, `std::nvm`). The transform has
                    // no place to put that tangent, so it says so instead of answering the zero of
                    // the space: a loop over a value with a tangent is refused, and a loop over a
                    // sequence stays exactly as it was.
                    const gc_node_ref_t iterableTangent = tangentOf[input];
                    if (iterableTangent != rt::kInvalidNodeRef &&
                        !zeroMade.contains(iterableTangent)) {
                        refusal.refusedOrigin = static_cast<camel::source::origin_id_t>(
                            function.nodeOrigin(ref));
                        refusal.refusedConstruct = "a loop over a value that has a tangent";
                        refusal.refusedWhy =
                            "the element the loop hands its body has the tangent of the array's "
                            "element, and no tangent of an element is carried";
                        return false;
                    }
                    loopInputs.push_back(copy);
                    continue;
                }
                if (in != nullptr && pairOf(in->dataType()) != nullptr) {
                    if (tangentOf[input] == rt::kInvalidNodeRef) {
                        complete = false;
                        break;
                    }
                    loopInputs.push_back(liftedPair(in->dataType(), copy, tangentOf[input]));
                } else {
                    loopInputs.push_back(copy);
                }
            }
            if (!complete) {
                break;
            }
            if (!loopInputs.empty()) {
                draft.setNormInputs(loopCopy, loopInputs);
            }
            // The first with-input is the body's result -- the new state; the rest (a round index,
            // what the body reads from outside) are copied as they are.
            std::vector<gc_node_ref_t> loopWith;
            loopWith.push_back(newState);
            const auto loopWithOriginals = function.withInputsOf(ref);
            for (size_t i = 1; i < loopWithOriginals.size(); ++i) {
                const gc_node_ref_t input = loopWithOriginals[i];
                if (input >= function.nodeCount() || !copied[input]) {
                    complete = false;
                    break;
                }
                loopWith.push_back(copyOf[input]);
            }
            if (!complete) {
                break;
            }
            draft.setWithInputs(loopCopy, loopWith);
        }
        if (!complete) {
            os << "REINTERP jvp function=" << name
               << " skip=" << refusalCauseName(RefusalCause::Loop) << '\n';
            noteRefusal(RefusalReading("jvp", name, RefusalCause::Loop));
            return fail(refusal);
        }
        // The value the copied chain produces: the node the original graph's exit names. The exit
        // node *is* the value-producing node of a compiled graph (its inputs are that node's
        // operands), so the value is the copy of the exit itself -- and a Gate only forwards, so it
        // is followed to what it carries.
        gc_node_ref_t oldValue = function.exitNodeRef();
        while (oldValue != rt::kInvalidNodeRef) {
            const NodeFace *node = function.node(oldValue);
            if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
                break;
            }
            const auto norm = function.normInputsOf(oldValue);
            if (!norm.empty()) {
                oldValue = norm.back();
                continue;
            }
            const auto with = function.withInputsOf(oldValue);
            if (!with.empty()) {
                oldValue = with.back();
                continue;
            }
            oldValue = rt::kInvalidNodeRef;
        }
        if (oldValue >= function.nodeCount() || !copied[oldValue]) {
            os << "REINTERP jvp function=" << name
               << " skip=" << refusalCauseName(RefusalCause::Value) << '\n';
            noteRefusal(RefusalReading("jvp", name, RefusalCause::Value));
            return fail(refusal);
        }
        const gc_node_ref_t chainValue = copyOf[oldValue];
        const gc_node_ref_t chainTangent = tangentOf[oldValue];
        // A body that ends at a branch already yields the pair -- the Join's type is the pair of the
        // value it yields -- so the exit is that value itself. A straight-line body yields a value
        // the rules gave a tangent for, and the exit is the pair built from the two.
        const rt::DraftNodeHeader *chain = draft.header(chainValue);
        TupleType *exitPair              = pairOf(valueTypeOf(oldValue));
        // Whether the node's copy *is* the pair, or is a tuple of pairs the exit has to take apart.
        // The type cannot answer: a two-state loop's value is `Tuple(pair, pair)`, the same type as
        // the pair of the whole value, so only `pairIsTheCopy` knows which of the two this is.
        const bool alreadyLifted = chain != nullptr && exitPair != nullptr &&
                                   chain->dataType == exitPair &&
                                   pairIsTheCopy.contains(oldValue);
        if (chainTangent == rt::kInvalidNodeRef && !alreadyLifted) {
            os << "REINTERP jvp function=" << name
               << " skip=" << refusalCauseName(RefusalCause::Tangent) << '\n';
            noteRefusal(RefusalReading("jvp", name, RefusalCause::Tangent));
            return fail(refusal);
        }
        // Route B (author's ruling of 2026-11-03 15:15: 抬升后的图一律以两元 tuple `Fill` 收尾): a
        // lifted graph always ends in a two-member tuple `Fill`, so `split` keeps its one cuttable
        // shape and its criterion is not widened by a second one.
        //
        // `alreadyLifted` above says the exit's copy *is* the pair -- but not why. When an
        // application answered it, the pair is a callee's answer and not a `Fill` this graph writes,
        // and that is the shape `split` refuses (`ExitShape::NotAPair`; measured 2026-11-03: the
        // exit node is the application itself, `kind=GCNodeKind::Func`, two `with` inputs and two
        // ordinary ones, its lifted copy's type `(float, float)`). Hydrating it is writing the two
        // halves this graph already holds into the shape: `valueOf`/`tangentOf` are the projections
        // of the call's pair, made where the call was lifted. Both members are written, so neither
        // half is a hole a reader has to resolve through the callee.
        //
        // Only an application is hydrated. A branch's Join and a one-state loop also answer with the
        // pair, and their exits are the shapes `split` already cuts as `Branch` and `Loop`; wrapping
        // those in a `Fill` would hide a region behind a tuple and turn a cuttable exit into a wall.
        const NodeFace *exitSource = function.node(oldValue);
        const bool exitIsApplication =
            exitSource != nullptr &&
            (exitSource->kind == rt::GCNodeKind::Call || exitSource->kind == rt::GCNodeKind::Func);
        const bool hydrateExitPair =
            alreadyLifted && exitIsApplication && valueOf[oldValue] != rt::kInvalidNodeRef &&
            tangentOf[oldValue] != rt::kInvalidNodeRef;
        // The exit is the lifted result: the value the copied chain computes and the tangent the
        // rules computed for it, which is the derivative of the chain at the values the caller
        // passed. Nothing is read back from the original graph: both halves are this graph's. A
        // node that hands its readers a value half (a lifted call, a loop of more than one state)
        // hands the exit the same half, so the pair is made of the two halves and not of the node.
        const gc_node_ref_t exitValue =
            !alreadyLifted && valueOf[oldValue] != rt::kInvalidNodeRef ? valueOf[oldValue]
                                                                      : chainValue;
        const gc_node_ref_t out =
            hydrateExitPair
                ? liftedPair(valueTypeOf(oldValue), valueOf[oldValue], tangentOf[oldValue])
                : (alreadyLifted ? chainValue
                                 : liftedPair(valueTypeOf(oldValue), exitValue, chainTangent));
        // The `sync` block's own ordering: in the original graph the exit passes through a GATE that
        // waits on the block's statements -- a print among them -- and that wait is the only thing
        // that keeps an effect whose result nobody reads from being dropped. The lifted exit is a
        // gate over the pair with the same waits, so the effect runs once, at the same point.
        gc_node_ref_t exit = out;
        const gc_node_ref_t originalExit = function.exitNodeRef();
        if (originalExit < function.nodeCount() && function.node(originalExit) != nullptr &&
            function.node(originalExit)->kind == rt::GCNodeKind::Gate) {
            Type *gateType =
                exitPair != nullptr ? static_cast<Type *>(exitPair) : valueTypeOf(oldValue);
            const gc_node_ref_t gate =
                draft.addGateNode(gateType, draft.allocateRuntimeSlot(gateType));
            draft.setNormInputs(gate, std::span<const gc_node_ref_t>(&out, 1));
            std::vector<gc_node_ref_t> waits;
            for (gc_node_ref_t edge : function.syncInputsOf(originalExit)) {
                if (edge < function.nodeCount() && copied[edge]) {
                    waits.push_back(copyOf[edge]);
                }
            }
            if (!waits.empty()) {
                draft.setSyncInputs(gate, waits);
            }
            if (function.node(originalExit)->hasFlag(rt::kGCNodeFlagSyncStatement)) {
                draft.setNodeRuntimeFlags(gate, static_cast<uint8_t>(rt::kGCNodeFlagSyncStatement));
            }
            exit = gate;
        }
        draft.setExitNode(exit);
        draft.setOutputNode(exit);
        draft.setReturnNode(exit, rt::GCReturnKind::Self);
        // The reading: one line per graph the lift made -- the function asked for and every callee it
        // had to lift -- so "a function called twice is lifted once" is countable, not claimed.
        std::string rules;
        for (const std::string &rule : usedRules) {
            rules += (rules.empty() ? "" : ",") + rule;
        }
        os << "REINTERP jvp function=" << name << " graph=" << rt::toValue(member)
           // The graph the lift was asked about. A run-time lift knows it -- it is the callee graph
           // the call named -- and prints its id there. A build-time lift does not and cannot: the
           // callee's runtime id is handed out when the program is encoded, which is after this
           // build, and a number here would read as the id of some graph. So the reading is a word
           // for "not assigned yet" rather than a number that looks like an answer.
           << " liftedFrom="
           << (rt::isValid(source) ? std::to_string(rt::toValue(source)) : std::string("unassigned"))
           << " type=" << signature->toString() << " nodes=" << function.nodeCount()
           << " kinds=" << copiedKinds << '/' << seenKinds
           << " exit=Tuple{chain,tangent} tangent=computed source=ruleset rules="
           << (rules.empty() ? "-" : rules) << '\n';
        // The reading the next step needs: the graph the lift has just finished, in one line.
        // `graph` is the member it was reserved as, `nodes` and `regions` are what the draft holds
        // at this moment -- before the group is encoded, which only numbers what is here -- and
        // `exit` is the node the graph answers with, the pair or the gate over it. Nothing is read
        // back and nothing is decided here: the line is printed and the build goes on exactly as it
        // did, so moving this build to compile time can be checked by the same numbers.
        os << "REINTERP jvp built graph=" << rt::toValue(member) << " nodes=" << draft.nodeCount()
           << " exit=" << exit << " regions=" << draft.regionCount() << " function=" << name
           << '\n';
    }
    return true;
}

} // namespace

} // namespace camel::transforms
