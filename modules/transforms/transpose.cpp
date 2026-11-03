// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose.h"

#include "jvp.h"
#include "rule_expand.h"
#include "split.h"

#include "camel/core/derivative.h"
#include "camel/core/forward.h"
#include "camel/core/loop_kind.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/text.h"
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
using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

/// The log's spelling of a cause. Nothing else in this file writes the word: a refusal site records
/// the cause and this spells it, so the log and the caller read one value twice instead of one
/// string being built in eighteen places and parsed back in a nineteenth.
const char *transposeRefusalWord(TransposeRefusalCause cause) {
    switch (cause) {
    case TransposeRefusalCause::None:
        return nullptr;
    case TransposeRefusalCause::Signature:
        return "signature";
    case TransposeRefusalCause::Exit:
        return "exit";
    case TransposeRefusalCause::Output:
        return "output";
    case TransposeRefusalCause::Component:
        return "component";
    case TransposeRefusalCause::Join:
        return "join";
    case TransposeRefusalCause::Condition:
        return "condition";
    case TransposeRefusalCause::ArmTangents:
        return "arm-tangents";
    case TransposeRefusalCause::Arm:
        return "arm";
    case TransposeRefusalCause::Rule:
        return "rule";
    case TransposeRefusalCause::RuleShape:
        return "rule-shape";
    case TransposeRefusalCause::RuleRanks:
        return "rule-ranks";
    case TransposeRefusalCause::Kind:
        return "kind";
    case TransposeRefusalCause::ParameterShape:
        return "parameter-shape";
    case TransposeRefusalCause::CotangentNeedsAValue:
        return "cotangent-needs-a-value";
    case TransposeRefusalCause::State:
        return "state";
    case TransposeRefusalCause::Range:
        return "range";
    case TransposeRefusalCause::CallNoTranspose:
        return "call-no-transpose";
    case TransposeRefusalCause::CallValueMissing:
        return "call-value-missing";
    case TransposeRefusalCause::CallValueComputedByTheBody:
        return "call-value-computed-by-the-body";
    case TransposeRefusalCause::UnreadParameter:
        return "unread-parameter";
    case TransposeRefusalCause::NeedsAValueOutsideTheClosure:
        return "needs-a-value-outside-the-closure";
    case TransposeRefusalCause::NoCotangentArithmetic:
        return "no-cotangent-arithmetic";
    }
    return nullptr;
}

namespace {

/// The transpose law that answers an operator here, chosen by the operands' own types, or the
/// refusal when the declaration does not choose.
///
/// A key holds one law in most families, and then this answers it. It holds several when one
/// spelling names several shapes: `t[0]` on a `T[]` is the element and on a `T[..]` it is the
/// open-rank array (design principles 5.4), so the index laws are two laws of one key with two
/// parameter-type lists. Which one answers is decided by the operands' own types, the way a call
/// decides -- an instance's ports have to take them (`Type::assignableFrom`) -- and among the laws
/// that take them the narrowest wins (`T[]` beside `T[..]`, `rulePortsOf`/`narrowerPortsThan`).
///
/// The question itself is not this file's: it is `rule_expand.cpp#chooseRuleAmong`, which `jvp`,
/// `vmap` and this transform all ask. What is this family's own is the instance tuple the ports are
/// read from -- the rule at `float`, which is what `expandRuleByName` instantiates
/// (`rule_expand.cpp`, whose `{Float64()}` is a standing defect recorded in
/// `docs/handover/reviews/questions-2026-10-29.md`, not this function's) -- and the words a refusal
/// is written with. A choice made on ports is only a choice between *shapes*: two laws of one key
/// that differ in their element type alone are told apart by the instance the expansion makes, not
/// here.
///
/// The answer travels back whole, `RuleChoice` and not a `bool`: the walk above writes the class of
/// a refusal into its record, and the class is not a wording choice. "No law takes this step" and
/// "a law was found and its shape says this step is not it" are two sentences with two owners --
/// the next step's gap, and this law's declared shape -- and only the choice knows which one it
/// made. Handing back a bare `why` made the walk re-derive the class from the text, or lose it; the
/// `showcase_s2` case that pins the difference is the one that fails when it is lost.
RuleChoice chooseTransposedRule(
    camel::core::context::Context &context, std::string_view uri,
    std::span<const gc_node_ref_t> inputs, size_t dataOperandCount,
    const std::function<const Type *(gc_node_ref_t)> &typeOf, const Type *cotangentType,
    const std::vector<bool> *answered) {
    std::vector<Type *> actual;
    actual.reserve(inputs.size());
    for (const gc_node_ref_t input : inputs) {
        actual.push_back(const_cast<Type *>(typeOf(input)));
    }
    // The instance the ports are read from is made at the types the right side will be built at,
    // in the order the law names them: the cotangent first, then the node's operands. A law written
    // per shape takes one type parameter per port (`matmul_t` serves the matrix and both vector
    // lanes), and asking it for one `float` answers `it takes 3 type parameter(s), and it was asked
    // for 1` -- the reading that made all three lanes unreachable behind a standing `{Float64()}`.
    std::vector<Type *> instanceTypes;
    if (cotangentType != nullptr) {
        Type *element = const_cast<Type *>(cotangentType);
        if (element->code() == camel::core::type::TypeCode::Array) {
            if (auto *array = tt::as_ptr<camel::core::type::ArrayType>(element)) {
                if (Type *zero = array->rankZeroType()) {
                    element = zero;
                }
            }
        }
        instanceTypes.push_back(element);
    }
    instanceTypes.insert(instanceTypes.end(), actual.begin(), actual.end());
    RuleChoiceWords words{"law", "operands"};
    // What is *not* a mismatch here is a rank: 5.4 says `T[..]` includes rank 0, a rank-zero array
    // being the element itself, so a port written `T[..]` takes a `float` and one law covers both
    // shapes `tensor:multiply` is called with. The clause this family used to keep -- an array port
    // refusing a non-array operand -- was deleted with the author's 2026-11-03 12:10 ruling, and the
    // array-and-scalar product has a law of its own now (`mul_tensor_t`, `lane-mulsc`).
    //
    // The instance the ports are read from is made at the operands' own types and not at one
    // `float`: a law is judged at the types the expansion will build it at, and a law that takes one
    // type parameter per shape (`matmul_t` serving the matrix and both vector lanes) has no instance
    // at all when it is asked for one `float` (`it takes 3 type parameter(s), and it was asked for
    // 1`). That sentence was this choice's reading on the merged tree -- the three lanes unreachable
    // behind it -- and it is what a standing `{Float64()}` costs once laws are written per shape.
    RuleChoice choice = chooseRuleAmong(
        context, camel::core::transposeRulesOf(std::string(uri)), instanceTypes, actual,
        dataOperandCount, words, answered);
    if (!choice.found() && choice.refusal != RuleRefusal::Shape) {
        // The law could not be built at one type per port. That is not this question's to refuse: a
        // law whose right side the expansion can still write is not a law that says this step is not
        // its shape, and refusing here reports the next step's gap as this one's. Ask again at the
        // **one** type the law is written for -- a law that names a single type parameter for
        // several positions (`add_t::<T>(x: T, c: T)`) is instantiated at one type, and the ports it
        // then has are that type's ports (`it takes 1 type parameter(s), and it was asked for 3`
        // is what one-per-port answers it).
        //
        // That one type is the step's own, and it was `Float64()` here until 2026-11-04: a narrow
        // float step then fell back to a `float` instance, whose ports take no `f32` operand, so the
        // choice refused by shape and the walk stopped -- measured, `vjp` of `(x: f32) => x + 1.5`
        // read `skip=rule-shape uri=:op/add_f reason=the rule takes (float, float), and the operator
        // has (f32, f32)`. The cotangent's element type is the step's own: an `f32` cotangent's
        // element is `f32`, a `float` cotangent's is `float`, and the operands of a step are the
        // types that cotangent is a cotangent of.
        Type *soleType = instanceTypes.empty() ? nullptr : instanceTypes.front();
        // No element type means there is no instance to ask about: the tuple stays empty -- the same
        // "nothing to try" the choice answers around, which the walk turns into `rule-shape` with the
        // operands it really has. A `float` here is an instance asked for a step whose cotangent is
        // not a `float`, which is the shape this whole change removes.
        std::vector<Type *> soleTypes;
        if (soleType != nullptr) {
            soleTypes.push_back(soleType);
        }
        choice = chooseRuleAmong(context, camel::core::transposeRulesOf(std::string(uri)),
                                 soleTypes, actual, dataOperandCount, words, answered);
        // `operandTypes` stay the node's own in both readings: the shape test is about the operands
        // the walk will hand the right side, whatever instance the ports were read from.
        if (!choice.found()) {
            choice.operandTypes = actual;
        }
    }
    if (!choice.found() && choice.refusal != RuleRefusal::Shape) {
        // The key files a law and no instance of it can be made at any tuple tried: a law written per
        // rank, meeting a rank outside the set it serves (`matmul_t` at `float[3, 2, 2]`). That is
        // not "no law for this step" -- the index has one -- so the walk is handed the law's name and
        // the expansion says what it could not build (`rule-ranks`). An empty key is the case this
        // class exists for, and it stays empty here.
        const std::vector<camel::parse::ast::RuleMatch> filed =
            camel::core::transposeRulesOf(std::string(uri));
        if (!filed.empty()) {
            choice.rule = filed.front().rule;
            choice.why.clear();
        }
    }
    return choice;
}

/// The type list as the index's reports write it: `(float[..], float)`, `()` when empty.
std::string typeTextOf(const std::vector<Type *> &types) {
    std::string text;
    for (const Type *type : types) {
        text += (text.empty() ? "(" : ", ");
        text += type != nullptr ? type->toString() : std::string("?");
    }
    return text.empty() ? std::string("()") : text + ")";
}

/// The ranks of the operands a cotangent was asked for, in the law's own order: `(3, 2)`.
///
/// The walk prints the ranks and not the types because the types are the expansion's to report and
/// the rank is what a law that dispatches on rank is declining. An operand that is not an array has
/// rank 0 -- it carries no extents to reduce and no lane to spread over -- and an open-rank array
/// (`float[..]`) reports the prefix it states, which is the number of axes the law can count on.
std::string
operandRanksText(const camel::runtime::GCGraph *function, std::span<const gc_node_ref_t> operands) {
    std::string text;
    for (const gc_node_ref_t operand : operands) {
        const camel::runtime::GCNode *node = function != nullptr ? function->node(operand) : nullptr;
        Type *type = node != nullptr ? const_cast<Type *>(node->dataType()) : nullptr;
        size_t rank                         = 0;
        if (type != nullptr && type->code() == camel::core::type::TypeCode::Array) {
            if (const auto *array = tt::as_ptr<camel::core::type::ArrayType>(type)) {
                rank = array->rank();
            }
        }
        text += text.empty() ? "(" : ", ";
        text += std::to_string(rank);
    }
    return text.empty() ? std::string("()") : text + ")";
}

/// Whether the rule named `ruleName` describes a node whose ports are `operandTypes`.
///
/// It does not, and this is why the question is asked here rather than left to the index: a rule is
/// filed under the head of its left side and that head's operand count, and the index does not read
/// the operand *types* (`rule_head.cpp`). So a law can be found for a node it was not written for --
/// a law whose parameters are two fixed-length arrays meeting an open-rank value is the shape that
/// has to be refused by name rather than expanded into something nobody wrote.
///
/// The choice of *which* law answers is `chooseRuleAmong`'s (`rule_expand.cpp`), and it asks this
/// same question one step earlier, at the instance `expandRuleByName` will build the right side
/// from. This is that question asked again at the types the expansion will really use -- the
/// cotangent's own type and the operands' -- because a law that covers two shapes of one operator
/// is judged at the types the node has, not at one `float` for every port. One criterion, two
/// instantiations: not two classifications.
///
/// What is *not* a mismatch is a rank. Design principles 5.4 says `T[..]` includes rank 0, that a
/// rank-zero array is the element itself, the same value and the same type. So a port written `T[..]`
/// takes a `float`, one law covers both shapes `tensor:multiply` is called with (two arrays, and an
/// array and a number), and the author ruled exactly that on 2026-11-03 12:10 -- the clause this file
/// used to keep (`sameOperandShape`, which refused a non-array operand for an array-written port) was
/// deleted with that ruling. A rank a law does not serve is a body that will not instantiate, which
/// is a different fact and has its own word (`RuleRanks`, `rule-ranks`).
///
/// The cotangent each position answers is then `Tangent<P>` of that position's own type, which is what
/// makes a number's cotangent a number: `sum_to(ct * x, w)` answers `w`'s own type (`S`, the type of
/// `like`), so the law has to be instantiated at the operand types the node really has
/// (`rule_expand.cpp#expandRuleByName`), not at one `float` for every port.
/// The type tuple a law has an instance at, from the types its ports are handed.
///
/// Usually the ports' own tuple. A law that names one type parameter for several positions
/// (`add_t::<T>(x: T, c: T)`, every scalar law of the family) has no instance at one type per port --
/// asking answers `it takes 1 type parameter(s), and it was asked for 3` -- and is instantiated at
/// **one** type instead: the step's own *element* type, which is the rank-zero type of the first
/// entry (5.4: the element of a rank-zero array type is the type itself, so this is the operand's own
/// type for a number and `float` for a `float` array).
///
/// That one type was `Float64()` here until 2026-11-04, which is why an `f32` step had no instance
/// whose ports take an `f32` operand and the walk refused by shape. Neither is it the first entry as
/// it stands: handing an array in as `T` gives a law that writes `T[..]` a port of `T[..][..]`, and
/// five array gradients then ran out of memory (measured). The tuple is decided here once, so the
/// shape test, the choice and the expansion cannot disagree about which instance is under discussion.
std::vector<Type *> lawInstanceTypes(
    camel::core::context::Context &context, const std::string &ruleName,
    const std::vector<Type *> &portTypes) {
    std::string unused;
    std::vector<Type *> types = portTypes;
    // A law instantiated at no operand type at all: there is nothing to ask and nothing to reduce,
    // so the tuple stays empty and the caller reads that as "no instance to try". A `float` pushed in
    // here would be an instance asked about a type no step has.
    if (types.empty()) {
        return types;
    }
    if (camel::compile::gir::genericRuleInstanceByName(context, ruleName, types, {}, unused) !=
        nullptr) {
        return types;
    }
    if (types.front() == nullptr) {
        return types;
    }
    Type *sole = types.front();
    if (sole->code() == camel::core::type::TypeCode::Array) {
        if (auto *array = tt::as_ptr<camel::core::type::ArrayType>(sole)) {
            if (Type *zero = array->rankZeroType()) {
                sole = zero;
            }
        }
    }
    const std::vector<Type *> single{sole};
    if (camel::compile::gir::genericRuleInstanceByName(context, ruleName, single, {}, unused) !=
        nullptr) {
        return single;
    }
    // Neither tuple has an instance: the ports' own is asked again, so the expansion says what it
    // could not build in its own words rather than being told about a tuple nobody chose.
    return types;
}

bool ruleTakesOperands(
    camel::core::context::Context &context, const std::string &ruleName,
    const std::vector<Type *> &operandTypes, const std::vector<bool> &answered,
    std::string &why) {
    std::string unused;
    // The same types the expansion asks for: a rule is judged at the types it will be built at.
    const std::vector<Type *> types = lawInstanceTypes(context, ruleName, operandTypes);
    const auto *instance =
        camel::compile::gir::genericRuleInstanceByName(context, ruleName, types, {}, unused);
    const FunctionType *ports =
        instance != nullptr && instance->lhs != nullptr ? instance->lhs->funcType() : nullptr;
    if (ports == nullptr) {
        // Nothing to check against: the expansion below says what it could not find, in its own
        // words, and this is not the place to invent a second spelling for "no instance".
        return true;
    }
    const size_t count = ports->normTypesCount();
    std::vector<Type *> wanted;
    wanted.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        wanted.push_back(ports->normTypeAt(i));
    }
    // A rule names the node's ports, in port order: the values, then the `<>` ones (`reshape<shape>`
    // names one of the two). Naming more ports than the node has cannot be filled; naming all of
    // the node's values is what a law for this operator does.
    const bool namesTheNode = count > 0 && count <= operandTypes.size();
    bool takes = namesTheNode;
    for (size_t i = 0; takes && i < count; ++i) {
        // A position the walk answers no cotangent for -- a constant, a value with no tangent space
        // -- is not a position the law is asked about (`isConstant`, `hasTangentSpace` in the walk),
        // so its shape cannot make the law wrong: `x * 2.0` is one array operand and one constant,
        // and the law for two arrays answers it correctly. What is checked is the operands the walk
        // will hand the right side.
        if (i < answered.size() && !answered[i]) {
            continue;
        }
        Type *const written = wanted[i];
        Type *const operand = operandTypes[i];
        if (written == nullptr || operand == nullptr) {
            takes = false;
            break;
        }
        // `assignableFrom` and nothing else: 5.4's rank 0 is the element, so an array port takes a
        // number, and the law that names an array operand is the law for that shape too.
        takes = written->assignableFrom(operand);
    }
    if (!takes) {
        why = "the rule takes " + typeTextOf(wanted) + ", and the operator has " +
              typeTextOf(operandTypes);
    }
    return takes;
}

/// The addition a cotangent space sums with, asked of the derivative registry -- the one place a
/// space is declared (`TangentSpace::addUri`, `camel/core/derivative.h`, design principles 9.9).
///
/// `:op/add_d` at `float` and `:op/add_f` at `f32` are that registry's answers, and so are
/// `:tangent/add_arr` for an array whose elements the core holds one slot each and `tensor:add` for
/// a tensor: the two kinds share the array *code* and sum differently, so the answer is read from
/// the space and never spelled here from a type code. A narrow float is a float but it is **not** a
/// `float` and not the same slot -- `f32` is four bytes and `float` is eight, so a cotangent written
/// as one and read as the other is a reinterpretation of bits, not a conversion.
///
/// An empty answer is "this space registers no addition": a cotangent that cannot be summed is not a
/// cotangent, and the caller refuses by name. It is **not** a `float`.
std::string addUriOfSpace(Type *space) {
    if (space == nullptr) {
        return {};
    }
    const camel::core::TangentSpace *registered =
        camel::core::DerivativeRegistry::instance().findTangentSpace(space);
    return registered != nullptr ? registered->addUri : std::string{};
}

/// The sum a cotangent accumulates with, and the type it is written at: the tangent space of the
/// value the cotangent lands in (`tangentSpaceOf`) and that space's own addition. Handing an array
/// to `add_d` is the same lie as calling it a float -- it reads the array as a number and takes the
/// process down, which is what two element reads of one array did (a sum of two reads was enough;
/// one read was not). Every accumulation in this file asks this question; a site that spells
/// `:op/add_d` itself decided the answer without asking.
///
/// `addType` is that space and never a placeholder: a `float` written here first is a `float` read
/// if the answer is ever consulted before it is filled. An empty `addUri` is a value whose type has
/// no cotangent space -- and every caller has already asked `hasTangentSpace` of the value before a
/// cotangent of it exists -- so an empty answer is a defect of this walk and refused as one.
std::string addUriFor(const camel::runtime::GCGraph *function, gc_node_ref_t ref, Type *&addType) {
    const camel::runtime::GCNode *node = function->node(ref);
    const Type *type                   = node != nullptr ? node->dataType() : nullptr;
    const camel::core::TangentSpaceAnswer space =
        type == nullptr ? camel::core::TangentSpaceAnswer{} : camel::core::tangentSpaceOf(type);
    addType = space.hasSpace() ? space.space : nullptr;
    return addUriOfSpace(addType);
}

/// Whether this transform has a cotangent for a result of type `result`: the cotangent is the
/// result's tangent space (`tangentSpaceOf`), and every width this file can add is a width it can
/// transpose at. Today that is the two scalars `float` and `f32` -- the arithmetic of the walk is the
/// addition above and the rules the family files, and those are written for the widths the language's
/// float operators come in (`:op/add_d`, `:op/add_f`). A result with no tangent space has no
/// cotangent to take in at all; a reduced-precision float has one, and this file has no addition for
/// it yet, so it is refused here instead of being answered with a `float` cotangent.
bool hasResultCotangent(const Type *result) {
    if (result == nullptr) {
        return false;
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(result);
    if (!space.hasSpace() || space.space == nullptr) {
        return false;
    }
    const camel::core::type::TypeCode width = space.space->code();
    return width == camel::core::type::TypeCode::Float64 ||
           width == camel::core::type::TypeCode::Float32;
}

/// The type a transposed function's cotangent input lives in: the cotangent of the result, which is
/// the result's tangent space. A `float` result answers `float` and an `f32` result answers `f32`
/// (`Tangent<f32>` is `f32`), which is the query and not a width written down here -- so widening
/// what `transpose` accepts cannot leave a `float` where an `f32` is promised (design principles
/// 9.9, and the drift `jvp`'s four sites were fixed for).
///
/// `nullptr` for a result with no tangent space. The gate above (`hasResultCotangent`) refuses such
/// a function before a signature is built from it, so a caller that meets a nullptr here is inside a
/// build the gate already admitted: it refuses rather than answer with a `float`.
Type *cotangentTypeOfResultType(const Type *result) {
    if (result == nullptr) {
        return nullptr;
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(result);
    return space.hasSpace() ? space.space : nullptr;
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

/// Whether `type` is `()`, the empty tuple. The distinction this draws is the one the whole answer
/// shape turns on, and it is a difference of *types*, not of tangent spaces: an `int` parameter and a
/// `()` parameter both have no tangent space, but an `int` is a value the caller still passes and
/// answers nothing, while `()` is a place with nothing to pass and an empty member of the answer
/// (`Tangent<int>` is `()` -- author, 2026-11-03: `()` is what the half **answers**, not what it
/// takes). Reading them as one thing either puts a port where nobody can fill it or drops a member
/// the declaration promised.
bool isUnitType(const Type *type) {
    if (type == nullptr || type->code() != camel::core::type::TypeCode::Tuple) {
        return false;
    }
    const auto *tuple = tt::as_ptr<camel::core::type::TupleType>(const_cast<Type *>(type));
    return tuple != nullptr && tuple->size() == 0;
}

FunctionType *transposeSignature(
    FunctionType *function, const std::vector<bool> &participates,
    std::vector<size_t> *answersOf, const std::vector<size_t> *captures) {
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
    // (design principles 9.9, `stdlib/types.cml#Tangent`), and the cotangent the caller passes, the
    // cotangent the answer's slots hold and the cotangent every rule is instantiated at are that one
    // type. A result whose cotangent this file has no arithmetic for is refused by `hasResultCotangent`
    // -- it is the same question `cotangentTypeOfResultType` answers below, asked once, before a
    // signature is built from it.
    if (!hasResultCotangent(function->exitType())) {
        return nullptr;
    }
    // The law, as the author rules it (2026-11-02 11:30): `transpose(g)` treats **every parameter
    // of `g` as a linear input** and answers `(y-bar) => (one cotangent per parameter)`. So no
    // parameter of `g` that takes a cotangent is a value its caller passes, and a body that only
    // reads such parameters gives a transposed function that takes the cotangent of the result and
    // nothing else. Two things stand before that cotangent instead of being passed as themselves:
    // a parameter whose type has no tangent space (an `int`, a `bool`, a string) is a value the call
    // site still fills, and so is a value the body reads from outside; which parameters those are is
    // a fact of the body, read below from the function rather than from its type.
    //
    // The declaration in `stdlib/types.cml#Transpose` is the contract (author, 2026-11-03,
    // `origin/fsh-dev` `6eceaa89d`): a declaration is what the builder makes, and no declaration is
    // approximate. That the two do not agree today is a missing capability of the type-bundle splice
    // -- multiplicity is lost, and the exact shape needs an operation saying which parameters stand
    // before the cotangent -- so this build is the side that has to move once the splice can write
    // it, and the difference is a defect to be closed rather than a wording to be kept. A fixed value a
    // body needs is not a parameter: it is
    // captured by the function that is transposed -- `transpose((dx) => f_lin(x, dx))(y-bar)` -- and
    // that is the caller's spelling. What it buys is that the type of a function says nothing about
    // which half of a pair it came from, so no bit has to be carried and the operator resolver --
    // which sees a type and nothing else -- has every fact it needs.
    //
    // `participates` is the one exception, and it is a fact of a *call site* rather than of a type:
    // where a call records which of its arguments are tangents, those are the parameters a cotangent
    // is answered for and the rest are values the caller still passes. The record is a position per
    // argument (`GCNode::tangentArgs`), not a count of trailing ones, because a rule writes a tangent
    // where its right side writes it. That is the shape a rule's tangent function has --
    // `mul_tangent(x, w, dx, dw)` is linear in `(dx, dw)`, so the cotangent of such a call is the
    // cotangent of its two tangents while its first two arguments remain values, and
    // `index_into(dxs, i)` is linear in its first argument while the index stays a value. Empty means
    // every parameter participates, which is the ordinary transposition of an entry.
    //
    // A parameter with no tangent space (an integer, a `bool`, a string) cannot be what a cotangent
    // is, and is read as a value: nothing enters or leaves its place in the answer. A function whose
    // parameters all lack a space has no transpose at all.
    const size_t count = function->normTypesCount();
    if (!participates.empty()) {
        // The participating-set form is a **call site's**: one flag per parameter of the callee,
        // saying which places a cotangent is answered for. What it requires is the same thing the
        // loop below requires of any signature, and nothing about the *types*: a place a cotangent is
        // answered for must have a tangent space (there is no type to put in the answer otherwise,
        // and the loop below refuses that place by name), and a place it is not is a value the call
        // site fills, of whatever type that value has.
        //
        // This used to demand `Type::Float64()` of **every** parameter, on the grounds that the form
        // began as a rule kernel's (`mul_tangent`'s four numbers) and "a function of plain numbers" is
        // what a kernel is. That is the record's shape, not the shape's: a rule's right side is linear
        // in its tangents and its values keep their own types, so a parameter that is not a number is a
        // value and not a reason to refuse. The array element's law is the one that shows it --
        // `index_into(xs: float[], i: int)` answers a cotangent for the array and reads the index, and
        // its member signature `(int, float[], float) => (float[],)` is that shape, built and run by
        // the type-based reading since before this form existed. Keeping the `float` demand while the
        // record named only a trailing run hid the difference, because the record was empty for that
        // call; once the record names positions it is not (measured 2026-11-04: `grad_array_no_int` and
        // eight others read `TRANSPOSE function=rulesets.jvp::index_into<float> skip=signature`).
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
        Type *const parameter = function->normTypeAt(i);
        const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(parameter);
        const bool participatesHere =
            participates.empty() ? (space.hasSpace() && space.space != nullptr) : participates[i];
        if (!participatesHere) {
            if (participates.empty() && isUnitType(parameter)) {
                // `()` is an **answer** with an empty slot, not a parameter: `(..Tangent<ParamsOf<F>>)`
                // has one member per parameter, so `Tangent<int>` is `()` and the pullback's answer
                // keeps the place even though nothing can land in it. It takes no port -- nobody can
                // pass a value where there is none. `grad_array_with_int.fvm` read `(float[],)` where
                // the declaration said `(float[], ())` for exactly this missing place. An `int`
                // parameter is *not* this: it is a value the caller still passes, so it falls through
                // to the value parameters below and answers nothing.
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
        withParams, params,
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

/// A call's recorded tangent-argument positions are read (`GCNode::tangentArgs`, one bit per
/// argument: which of the call's arguments a rule's expansion said are tangents). A kernel call inside
/// an arm is transposed as the linear map it is -- the values are ports, the tangents are what the
/// answer is a cotangent of, and the call site passes the values and the cotangent only. Before this,
/// the record is unread, the call is a plain function of all its parameters, and the arm stops at the
/// tangent that has no value: `arm-stop region=1 node=7 arg=1 callee=autodiff::mul_tangent<float>`.
/// The A/B of the two readings.

/// A callee that arrives as a *value* is named. `calleeIdOf` answers for a FUNC node -- a call
/// written by name; a function value put into a `let` and called is a DATA node holding a static
/// function, and `GCGraph::staticFunctionGraphOf` is the one entry that turns it into a graph id (or
/// says which check refused: not a data node, no static slot, not a function type, no function value,
/// no graph, not a static reference of this graph). Before this, that shape refuses with
/// `skip=call callee=none`, which is what the transform did before this entry existed -- the A/B of
/// `transpose_function_value_no_capture.cml` flips exactly this. A value with no capture names its
/// graph and transposes; a closure names its graph too, and then fails later, on the capture. (The
/// switch that guarded this is gone; the entry it guarded is the one the cases read.)

/// The extents a type's shape states, or nothing when the type leaves one of them open.
///
/// A shape is a fact about an array type: `float[2, 3]` states two extents, `float[]` states one
/// dimension and leaves its length open, and `float[..]` states neither the rank nor a length. The
/// reading of a stated extent is the one `jvp.cpp#zeroConstantFor` makes of the same fact --
/// `ExtentForm::evaluate({})` answers the number a written extent holds and nothing for a `?` or for
/// an extent over a name nothing binds -- so the two transforms agree about which types have a shape
/// that can be written while the graph is built.
std::optional<std::vector<int64_t>> statedShapeOf(const Type *type) {
    if (type == nullptr || type->code() != camel::core::type::TypeCode::Array) {
        return std::nullopt;
    }
    const auto *array = tt::as_ptr<camel::core::type::ArrayType>(const_cast<Type *>(type));
    if (array == nullptr || array->elemType() == nullptr ||
        !camel::core::type::isFloatingScalar(array->elemType()->code())) {
        return std::nullopt;
    }
    const std::span<const camel::core::type::ExtentForm> stated = array->extents();
    if (stated.empty()) {
        // `T[..]`: the rank is the value's, so the type states no shape at all.
        return std::nullopt;
    }
    std::vector<int64_t> extents;
    extents.reserve(stated.size());
    for (const camel::core::type::ExtentForm &extent : stated) {
        const std::optional<int64_t> size = extent.evaluate({});
        if (!size.has_value() || *size < 0) {
            return std::nullopt;
        }
        extents.push_back(*size);
    }
    return extents;
}

/// The value a law's right side is handed for a parameter a cotangent is answered for: the zero of
/// the parameter's type, which is that type's **shape** and nothing else.
///
/// `transposeSignature` gives the transposed function the cotangent and no parameter, so a law that
/// reads the parameter -- `mean_axis_cotangent` reads `xs` for `numel` and `ones_like` -- has the type
/// and not the argument. What the type says is the shape, and the shape is what a transposition needs:
/// the adjoint of a linear map is a function of the cotangent and of the domain's shape (author,
/// 2026-11-03 16:15). The value below carries that shape; every element of it is zero, which is the
/// one thing here that is not the argument. A law that reads an *element* would therefore be answered
/// with zeros -- and such a law is not a law of a transposition, because a transposition has no
/// element of the argument to read. This is the same zero `jvp` gives a value that does not move
/// (`jvp.cpp#zeroConstantFor`).
///
/// Nothing when the type leaves an extent open: then the shape is written down nowhere, and the
/// caller refuses by name instead of inventing one.
gc_node_ref_t shapeCarrierOf(camel::runtime::GraphDraft &draft, Type *type) {
    const std::optional<std::vector<int64_t>> extents = statedShapeOf(type);
    if (!extents.has_value()) {
        return rt::kInvalidNodeRef;
    }
    auto *array = static_cast<camel::core::type::ArrayType *>(type);
    ::Array *zero = ::Array::create(
        mm::autoSpace(), array, std::span<const int64_t>(*extents), /*zeroInit=*/true);
    if (zero == nullptr) {
        return rt::kInvalidNodeRef;
    }
    return draft.materializeStaticValue(
        camel::core::rtdata::toSlot<::Array *>(zero), type, rt::kGCNodeFlagConstant);
}

/// The zero a cotangent space starts from, asked of that space: the constant the registry holds
/// (`TangentSpace::zero`, written in the space's own width -- an `f32` zero is four bytes and a
/// `float` zero is eight, and one read as the other is a reinterpretation of bits), or the space's
/// own `zerosLikeUri` for a space whose zero is built from a value's shape (`:tangent/zeros_arr` for
/// an array of values, `tensor:zeros_like` for a tensor). Both are the registry's answers, so no
/// width and no operator name is written down here (design principles 9.9).
///
/// `shapeFrom` is the value the shape is read from, and it has to be a node of **this** graph: a ref
/// of the source graph written into a node of the new one reads whatever node sits at that index,
/// which is how a zero array came to be built from a number (`autodiff: a zero tangent array needs
/// an array with a tangent`). `kInvalidNodeRef` builds the shape from the type's stated extents
/// (`shapeCarrierOf`), which is what a zero needs in order to be an array of the right shape.
///
/// An invalid ref for a space the registry does not know, or one with neither a zero constant nor a
/// `zerosLikeUri`: a cotangent that has no zero cannot be accumulated, and the caller refuses by name
/// instead of handing the slot a `float` zero.
gc_node_ref_t zeroOfSpaceOf(
    camel::runtime::GraphDraft &draft, Type *space, gc_node_ref_t shapeFrom) {
    if (space == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const camel::core::TangentSpace *registered =
        camel::core::DerivativeRegistry::instance().findTangentSpace(space);
    if (registered == nullptr) {
        return rt::kInvalidNodeRef;
    }
    if (registered->zero.has_value()) {
        return draft.materializeStaticValue(*registered->zero, space, rt::kGCNodeFlagConstant);
    }
    if (registered->zerosLikeUri.empty()) {
        return rt::kInvalidNodeRef;
    }
    if (shapeFrom == rt::kInvalidNodeRef) {
        shapeFrom = shapeCarrierOf(draft, space);
    }
    if (shapeFrom == rt::kInvalidNodeRef) {
        return rt::kInvalidNodeRef;
    }
    const gc_node_ref_t zeros =
        draft.addOperNode(space, nullptr, registered->zerosLikeUri.c_str());
    draft.setNormInputs(zeros, std::span<const gc_node_ref_t>(&shapeFrom, 1));
    return zeros;
}

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
    /// Why the walk stopped, recorded at the site that stopped it. A probe carries a state of its
    /// own, so a probe's refusal never becomes the answer of the build that made it.
    TransposeRefusal refusal;
    /// The parameter whose *shape* the walk had to ask for and whose type does not state it: the
    /// cause the build refuses on when the member comes out with a port the operator's type has no
    /// room for. It is a fact and not a refusal: a call site inside a body carries the value (the
    /// capture is one of the member's ports, and the caller's type is written with it in hand), so
    /// asking is legitimate there and the walk must keep asking -- refusing at the step would break
    /// every `index_into` over an array whose extent only the caller knows. Whether the ask is fatal
    /// is a fact about the whole member, and `buildTransposeGraph` decides it once the ports are
    /// known (author, 2026-11-03 16:15: the shape has to be in the type or derivable from `ct` and
    /// known values -- and a value the caller carries *is* one).
    bool shapeWanted = false;
    /// The parameter the ask was about, and the name and type its declaration writes, for the
    /// sentence: the repair is that spelling. Recorded where the ask is made, because that is the
    /// one place that has the parameter and its declared type together.
    size_t shapeParam = 0;
    std::string shapeName;
    std::string shapeType;
};

/// One call of a callee that answers the pair (value, tangent), as a call site describes it to
/// `transposePairCall` below: the graph that is called, the call's own arguments, and the cotangent of
/// the pair's **tangent** component -- which is the only component a linear half has.
struct PairCall {
    camel::runtime::GCGraph *callee = nullptr;
    /// The source graph's call node: the place a refusal names.
    gc_node_ref_t ref = rt::kInvalidNodeRef;
    /// The call's ordinary arguments in the source graph, in the callee's parameter order.
    std::vector<gc_node_ref_t> arguments;
    /// The call's context (`with`) arguments in the source graph, in the callee's `with` order.
    std::vector<gc_node_ref_t> withArguments;
    /// This call's cotangent of the answer's tangent component.
    gc_node_ref_t cotangent = rt::kInvalidNodeRef;
};

/// Transposes a call of a callee that answers the pair (value, tangent) **by the content of that
/// callee**, at whichever path the call stands on -- the straight-line body and an arm both call this,
/// so the two cannot drift apart, and nothing here reads a record of where the callee came from
/// (author, 2026-11-03 16:55: a record of the graph a lift was taken from is the counter-example --
/// a transformation's result must depend on the content of its input graph, and on nothing else).
///
/// The rule: the callee is cut (`buildSplitGraph`), the **second half** is transposed, and the call is
/// turned into a call of that transposition. The second half is linear in the tangents, which is what
/// makes this the transposition of the call: the cotangent of the pair's tangent is what its
/// transposition takes, and the cotangents it answers for the tangents are where the call's tangent
/// arguments' cotangents go.
///
/// The second half reads the values the forward half computed, and it reads them **out of its
/// closure** -- so the call site does not assemble a residual argument list: it closes the second
/// half's own graph over the values it stands on (`buildLinearClosure`, the writer the cut itself uses)
/// and calls the transposed member through that closure value. The values are found by identity, not by
/// position: a residual that is a parameter is that parameter's argument, and a residual that is a
/// `with` port is that `with` argument. A residual neither of those is a value the callee computes
/// inside itself, which this call does not stand on -- saying that is a refusal by name, never a guess
/// at a nearby argument.
///
/// `forward` places a source value on the transposed side of the cut (the two paths spell that
/// differently); `deliver` hands a cotangent to a source argument.
bool transposePairCall(
    TransposeState &state, rt::GraphDraft &draft, const PairCall &call,
    const std::function<gc_node_ref_t(gc_node_ref_t)> &forward,
    const std::function<void(gc_node_ref_t, gc_node_ref_t)> &deliver, std::ostream &os,
    std::string &why);

bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, const GraphId member,
    std::ostream &os, const std::vector<bool> &participates = {},
    std::vector<size_t> *needs = nullptr) {
    camel::runtime::GraphDraftGroup &group = state.group;
    if (state.program == nullptr || function == nullptr) {
        return false;
    }
    // One place says why, and it says it as a value: the cause is recorded for the caller, and the
    // log's line is rendered from the same cause. A site adds the *where* -- the node, the uri, the
    // callee -- because that is not the cause and no caller decides anything from it; a site that
    // wanted a sentence of its own would add an enumerator instead of writing one here.
    //
    // The graph is part of the where and is recorded here rather than at the sites, because every
    // refusal is met inside a graph and the lambda is the one place that has it: the sentence keeps
    // saying "this function" only because the name never left the log before.
    const auto refuse = [&](TransposeRefusalCause cause,
                            std::optional<size_t> param = std::nullopt) -> void {
        state.refusal = TransposeRefusal{cause, param};
        state.refusal.graph = function->name();
        os << "TRANSPOSE function=" << function->name() << " skip=" << transposeRefusalWord(cause);
    };
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
        // `found` is what this half needs and is recorded on the state; nothing prints it. A
        // probe line here was left by the `8194d89fd` experiment and made every reading carry a
        // line no cause asked for.
        state.calleeNeeds[function] = found;
        // The ask is the probe's, so the fact it was a parameter's *shape* is the probe's too: the
        // member being built here answers those parameters from the capture ports `found` names and
        // never reaches the ask, so a fact recorded only there would be lost with the probe's state.
        // Read here for the same reason `found` is: one discovery, not two.
        if (probe.shapeWanted && !state.shapeWanted) {
            state.shapeWanted = true;
            state.shapeParam  = probe.shapeParam;
            state.shapeName   = probe.shapeName;
            state.shapeType   = probe.shapeType;
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
        refuse(TransposeRefusalCause::Signature);
        os << '\n';
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
    // Which parameter a normal port of this function stands for, and which port a parameter asks for.
    // **One walk, and every reader below asks it**: `normPorts()` position `i` is
    // `portParameter[i]`, and a parameter asks `portOfParameter[at]`. A `()` place is an answer and
    // not a parameter, so it is the one thing skipped: a graph that declares a port for every other
    // parameter -- a plain function, an `int` included -- degenerates to the identity, while a half's
    // graph declares a port only for a parameter that has a tangent space (`buildSplitGraph`: "a ()
    // slot is an answer this half gives, never an argument it takes") and its type keeps the place as
    // `()`, so its port `i` is the parameter index among the non-`()` places and **not** the `i`-th
    // parameter: `f(n: int, xs: float[])`'s backward half is `((), float[])` with one port, `dxs`,
    // which is parameter 1. Reading a port index as a parameter index is the arithmetic this
    // replaces, and it is wrong exactly when a `()` place stands before a parameter with a port.
    std::vector<size_t> portParameter;
    std::vector<size_t> portOfParameter(paramCount, paramCount);
    for (size_t at = 0; at < paramCount; ++at) {
        if (isUnitType(function->funcType()->normTypeAt(at))) {
            continue;
        }
        portOfParameter[at] = portParameter.size();
        portParameter.push_back(at);
    }
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
                if (isUnitType(portType)) {
                    // `()` is a member of the answer and not a parameter: nobody can pass a value
                    // where there is none, so it takes no port. An `int` parameter is a value the
                    // caller still fills and is not this (`isUnitType`).
                    continue;
                }
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
            if (declared == nullptr) {
                // The capture port takes the residual's own declared type: a port typed `float` for
                // a residual that is not one hands the value to a slot of another width, and a
                // residual with no type at all is a graph whose closure cannot be declared. The
                // node's declaration is the only source of that type, so the walk refuses instead of
                // inventing a `float`.
                refuse(TransposeRefusalCause::Signature);
                os << " closure=" << ref << '\n';
                return false;
            }
            Type *const type = const_cast<Type *>(declared);
            const gc_node_ref_t port =
                draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendClosureNode(port);
            closurePorts.push_back(port);
            closurePortOf[ref] = port;
        }
        // The source's `with` parameters, as `with` ports of this graph: the value a call supplies,
        // read where the body read it. No cotangent is answered for one -- it is a constant of the
        // differentiation -- so these ports are reads only, and the answer tuple has no member for
        // them.
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> withPortOf;
        {
            const size_t withCount =
                function->funcType() != nullptr ? function->funcType()->withTypesCount() : 0;
            const std::span<const gc_node_ref_t> sourceWith = function->withPorts();
            for (size_t i = 0; i < withCount && i < sourceWith.size(); ++i) {
                camel::core::type::Type *withType = function->funcType()->withTypeAt(i);
                if (withType == nullptr) {
                    continue;
                }
                if (function->funcType()->withIsVarAt(i)) {
                    if (camel::core::type::Type *value =
                            camel::core::type::valueTypeOf(withType)) {
                        withType = value;
                    }
                }
                const gc_node_ref_t port =
                    draft.addPortNode(withType, draft.allocateRuntimeSlot(withType));
                draft.appendWithPort(port);
                withPortOf[sourceWith[i]] = port;
            }
        }
        if (carriesClosure && function->closureType() != nullptr) {
            draft.setClosureType(const_cast<camel::core::type::TupleType *>(function->closureType()));
        }
        // Where a parameter's own value sits among the residuals, and why a *parameter's number* is the
        // place rather than a type. `residualsOf` numbers the residuals as the parameters by their
        // index and then the values the body computed (`modules/transforms/split.cpp`, "program
        // order"), one residual per parameter the tangent side reads -- so slot `at` is parameter
        // `at`'s value, and this graph declares one closure port per residual in that order. Which
        // parameter this is therefore decides the slot: two arrays of one element type are two
        // parameters, and the shape an answer must be written in is *this* parameter's, not the
        // first member's of that type.
        // A closure member is forwarded to the member the transposed graph carries, and nothing
        // about where it came from is asked (author, 2026-11-03 08:50). A member the cut wrote and
        // a member a caller closed by hand are the same shape -- a port the function is handed --
        // so both answer the same way, and a transposition does not need to know which parameter a
        // member was the value of.
        // The cotangent input's type is the result's own cotangent space, and a function whose
        // result has one is the only function a member is built for (`transposeSignature`'s gate
        // admits exactly those). A null here is a caller that asked for a member of a function the
        // gate refuses: refused by name, never typed `float`.
        Type *const cotangentType = resultCotangentType(function);
        if (cotangentType == nullptr) {
            refuse(TransposeRefusalCause::Exit);
            os << " exit="
               << (function->funcType() != nullptr && function->funcType()->exitType() != nullptr
                       ? function->funcType()->exitType()->toString()
                       : std::string("?"))
               << '\n';
            return false;
        }
        const gc_node_ref_t cotangentIn =
            draft.addPortNode(cotangentType, draft.allocateRuntimeSlot(cotangentType));
        draft.appendNormPort(cotangentIn);

        const auto primitiveOf = [&draft](Type *type, const char *uri, std::vector<gc_node_ref_t> inputs) {
            const gc_node_ref_t made = draft.addOperNode(type, nullptr, uri);
            draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
            return made;
        };
        // A value the cotangent path does not carry but the transposed graph must reproduce: a
        // branch's condition is computed from the parameters, and the arm that ran is decided by
        // it. These nodes are copied forward, not transposed. Declared here because the zero of a
        // cotangent space reads a value's shape from it (`zeroOf`).
        std::vector<gc_node_ref_t> forwardOf(nodeCount, rt::kInvalidNodeRef);
        // Every cotangent accumulation in this file goes through this one function: given the cotangent
        // space of the value a cotangent lands on, what the place holds already (invalid means nothing
        // yet) and what arrives, it answers the value that place holds afterwards. The sum is that
        // space's own (`addUriOfSpace`: the registry's `addUri`), so an `f32` accumulates with
        // `:op/add_f`, an array with `:tangent/add_arr` and a tensor with `tensor:add`; nothing here
        // spells an addition of its own, and no width is written down as a placeholder.
        //
        // The space is the *value's*, handed in by the caller that knows the value -- not `into`'s
        // space: `into` is a node of the **new** graph, and asking the source graph about it reads
        // whatever source node sits at that index, a type that has nothing to do with the cotangent
        // being summed. That read answered `:op/add_d` for as long as every cotangent was a `float`;
        // asked of the registry it answers nothing at all, which is how it was found.
        //
        // A space with no addition at all is a cotangent that cannot be summed:
        // `cotangentArithmeticMissing` records it so the member is refused by name at the end of the
        // walk (`NoCotangentArithmetic`) instead of handing the slot a `float` sum.
        bool cotangentArithmeticMissing = false;
        const auto accumulate = [&](Type *space, gc_node_ref_t into,
                                    gc_node_ref_t node) -> gc_node_ref_t {
            if (into == rt::kInvalidNodeRef) {
                return node;
            }
            const std::string addUri = addUriOfSpace(space);
            if (addUri.empty() || space == nullptr) {
                cotangentArithmeticMissing = true;
                os << "TRANSPOSE function=" << function->name() << " cotangent-no-arithmetic"
                   << '\n';
                return rt::kInvalidNodeRef;
            }
            return into == node ? primitiveOf(space, addUri.c_str(), {node, node})
                                : primitiveOf(space, addUri.c_str(), {into, node});
        };
        // The zero of the space a value's cotangent lives in, asked of the space (see
        // `zeroOfSpaceOf`): a floating scalar's is the constant the registry holds in that scalar's
        // own width -- an `f32` zero is four bytes, not a `float` zero read as four -- and a space
        // whose zero is built from a value's shape uses the space's own `zerosLikeUri`. Nothing here
        // writes a `float` zero on behalf of a type that is not a `float`.
        //
        // The shape a zero array is built at is a node of **this** graph: the value the walk
        // forwarded for it when there is one, and the type's own stated extents otherwise. A source
        // ref written into a node of the new graph reads whatever node sits at that index.
        const auto zeroOf = [&](gc_node_ref_t valueRef) -> gc_node_ref_t {
            const camel::runtime::GCNode *node = function->node(valueRef);
            const Type *type                   = node != nullptr ? node->dataType() : nullptr;
            const camel::core::TangentSpaceAnswer space =
                type == nullptr ? camel::core::TangentSpaceAnswer{}
                                : camel::core::tangentSpaceOf(type);
            const gc_node_ref_t forwarded =
                valueRef < forwardOf.size() ? forwardOf[valueRef] : rt::kInvalidNodeRef;
            return zeroOfSpaceOf(draft, space.hasSpace() ? space.space : nullptr, forwarded);
        };
        // The type a value's cotangent lives in, read from the value's type: the same question
        // `jvp` asks of a parameter (`camel/core/tangent_space.h`), asked here of a loop's state and
        // of a value that escaped it. A `float[]` state pulls back an array; a number pulls back a
        // number. Nothing in this file decides that by hand any more -- the port, the tuple member
        // and the component read all ask here.
        //
        // There is **no fallback**: a ref that is not in the source graph, or a value whose type has
        // no tangent space, has no cotangent type, and every caller refuses by name. A `float`
        // written here is the whole family's defect -- it answers "this cotangent is a `float`" for
        // values whose cotangent is anything else, and the answer is read.
        const auto cotangentTypeOfType = [](const Type *type) -> Type * {
            if (type == nullptr) {
                return nullptr;
            }
            const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(type);
            return space.hasSpace() ? space.space : nullptr;
        };
        const auto cotangentTypeOf = [function, &cotangentTypeOfType](gc_node_ref_t ref) -> Type * {
            const camel::runtime::GCNode *node = function->node(ref);
            return cotangentTypeOfType(node != nullptr ? node->dataType() : nullptr);
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
            // A `with` port is a value the call supplies and the answer says nothing about: the
            // transposition answers one cotangent per ordinary parameter (`Transpose<F>`), and a
            // weight is not one.
            if (withPortOf.find(target) != withPortOf.end()) {
                return false;
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
            Type *addType            = nullptr;
            const std::string addUri = addUriFor(function, ref, addType);
            if (addUri.empty() || addType == nullptr) {
                // Two cotangents land on one value and the value's space registers no addition:
                // there is no sum to write, so this member answers no cotangent. Recorded here and
                // refused by name at the end of the walk (`NoCotangentArithmetic`); a `float` sum
                // here would be an addition the space never declared.
                cotangentArithmeticMissing = true;
                os << "TRANSPOSE function=" << function->name()
                   << " cotangent-no-arithmetic node=" << ref << '\n';
                return;
            }
            if (cotangent[ref] == node) {
                cotangent[ref] = primitiveOf(addType, addUri.c_str(), {node, node});
                return;
            }
            cotangent[ref] = primitiveOf(addType, addUri.c_str(), {cotangent[ref], node});
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
                                       gc_node_ref_t ct, Type *ctType, std::string &why,
                                       bool &noInstance) -> gc_node_ref_t {
            const auto valueOf = [&](size_t slot) -> gc_node_ref_t {
                return slot < inputs.size() ? knownValue(inputs[slot]) : rt::kInvalidNodeRef;
            };
            // The operands' own types: the cotangent of a parameter is the parameter's tangent
            // space, so the law is built at the types the operator really has. A node that is not in
            // the graph answers no type, and the expansion falls back to the single-type instance.
            std::vector<Type *> operandTypes;
            // The cotangent's own type first: a law that covers two shapes of one operator writes
            // `(ct: T[..])`, and `T` has to be the type the cotangent really has -- a number here,
            // an array there -- or the right side's operator is resolved for the wrong one.
            // `T` is the law's *element* type: a cotangent whose type is a rank-zero array type is the
            // element itself (5.4), and the law writes `T[..]` around it. Handing the rank-zero type
            // in as `T` would ask for `float[..][..]`.
            Type *ctElement = ctType;
            if (ctType != nullptr && ctType->code() == camel::core::type::TypeCode::Array) {
                if (auto *array = tt::as_ptr<camel::core::type::ArrayType>(ctType)) {
                    if (Type *zero = array->rankZeroType()) {
                        ctElement = zero;
                    }
                }
            }
            operandTypes.reserve(inputs.size() + 1);
            operandTypes.push_back(ctElement);
            for (const gc_node_ref_t input : inputs) {
                const camel::runtime::GCNode *inputNode = function->node(input);
                operandTypes.push_back(inputNode != nullptr
                                           ? const_cast<Type *>(inputNode->dataType())
                                           : nullptr);
            }
            // The tuple the right side is expanded at: the one the law has an instance at, decided by
            // the same question the shape test and the choice ask (`lawInstanceTypes`), so the three
            // cannot disagree about which instance is under discussion.
            //
            // The expansion falls back to a single `float` instance on its own when the tuple it is
            // handed has none, and at an `f32` step that `float` instance is a lie: measured on
            // 2026-11-04, `transpose_of_autodiff::mul_tangent<f32>` passed `a f32 as ordinary argument
            // 0 of autodiff::add_cotangent<float>, which takes a float`, and the scheduler stopped on
            // the edge invariants. Handing it the step's own element type is what keeps the right side
            // in the width the cotangent is in. A law with no instance at either tuple is expanded at
            // the ports' own types, and the expansion says what it could not build in its own words.
            const std::vector<Type *> at = lawInstanceTypes(*state.context, ruleName, operandTypes);
            return camel::transforms::expandRuleByName(
                *state.context, group, group.transaction(member), ruleName, valueOf, {ct}, position,
                at, why, &noInstance);
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
        // Whether the last `forward` attempt stopped at a node kind it cannot copy -- the owner of a
        // region, a loop or a branch. That is the whole difference between the two shapes a call
        // site can fail on, so it is recorded where the walk stops rather than guessed at the call.
        bool stoppedAtARegion = false;
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
                // A `with` parameter of the source: the transposed graph declares the same port, and
                // that is where the value comes from. It is not a residual the cut carries and not a
                // cotangent this transform answers for.
                if (const auto with = withPortOf.find(ref); with != withPortOf.end()) {
                    forwardOf[ref] = with->second;
                    return forwardOf[ref];
                }
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
                    // The port at position `i` of the original graph is the parameter
                    // `portParameter[i]`, asked of the one walk above -- `i` for a plain graph, and
                    // the `i`-th parameter with a tangent space for a half's graph, whose `()`
                    // places take no port. Everything below indexes the *parameter*: `portOfParam`,
                    // `capturePortOf` and `transposeSignature`'s capture list are all keyed that
                    // way, the last one through `funcType()->normTypeAt(at)`, so a port index read
                    // as a parameter index names a different place whenever a parameter without a
                    // tangent space stands before one with a tangent space.
                    const size_t at =
                        i < portParameter.size() ? portParameter[i] : paramCount;
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
                        if (forwardOf[ref] == rt::kInvalidNodeRef && at < paramCount) {
                            // The parameter's own value, by **identity**: the closure slot that is this
                            // parameter's value, and the port there is the value. Which parameter
                            // this is decides the slot -- two arrays of one element type are two
                            // parameters, and the shape an answer must be written in is *this*
                            // parameter's, not the first member's of that type. `xs[0] + ys[0]` over
                            // arrays of two lengths answered `gy` sized by `xs` while the member was
                            // chosen by type. The slot's type is **checked**, never searched: a slot
                            // that does not hold a value of the parameter's type refuses instead of
                            // handing over a neighbouring member.
                            const size_t slot   = at;
                            Type *const askType = function->funcType()->normTypeAt(at);
                            const camel::runtime::GCNode *slotMember =
                                slot < closureOf.size() ? function->node(closureOf[slot]) : nullptr;
                            if (askType != nullptr && slotMember != nullptr &&
                                slotMember->dataType() != nullptr && slot < closurePorts.size() &&
                                camel::core::type::typeEquals(slotMember->dataType(), askType)) {
                                forwardOf[ref] = closurePorts[slot];
                                return forwardOf[ref];
                            }
                        }
                        if (forwardOf[ref] == rt::kInvalidNodeRef && i < paramCount) {
                            // Neither a port nor a closure slot hands this parameter in, and the
                            // law's right side reads its value. That value is the **shape** the
                            // parameter's type states, and nothing else: the transposed function is
                            // given the cotangent alone, so a law that reads `xs` for `numel` or
                            // `ones_like` is answered from the type. A parameter whose shape the type
                            // states (`float[2, 3]`) is answered with a zero array of that shape --
                            // a *shape carrier*, which is the only thing a law that reads `xs` for
                            // `numel` or `ones_like` can ask of a value this graph does not have.
                            Type *const askType = function->funcType()->normTypeAt(at);
                            if (const gc_node_ref_t carrier = shapeCarrierOf(draft, askType);
                                carrier != rt::kInvalidNodeRef) {
                                forwardOf[ref] = carrier;
                                return carrier;
                            }
                            if (askType != nullptr &&
                                askType->code() == camel::core::type::TypeCode::Array) {
                                // The type does not state the shape, so nothing can be answered from
                                // it: the walk asks for the value below, as it does for every value a
                                // law reads, and the *build* decides whether that ask fits the type
                                // the operator promised (`buildTransposeGraph`). Refusing here would
                                // be wrong for a call site inside a body, where the caller carries
                                // the value and the member's type is written with it: the adjoint is
                                // then a function of `ct` and of a known value, which the rule allows.
                                // What is recorded is the fact the repair is about -- which parameter,
                                // and the declaration that should state its shape.
                                if (!state.shapeWanted) {
                                    state.shapeWanted = true;
                                    state.shapeParam  = at;
                                    state.shapeType   = askType->toString();
                                    const FunctionType *const signatureType = function->funcType();
                                    if (signatureType->hasArgNames()) {
                                        const size_t named = signatureType->withTypesCount() + at;
                                        if (named < signatureType->argNamesCount()) {
                                            state.shapeName =
                                                std::string(signatureType->argNameAt(named));
                                        }
                                    }
                                }
                            }
                        }
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
                // A kind `forward` cannot copy: a loop's rounds, a branch's arms -- a value that
                // comes out of a **region**. Recorded here rather than guessed from the kind of the
                // node a caller happens to have in hand, because the caller's node may be a
                // projection *of* such a value (`Accs` over a loop's cell): the walk stops at the
                // region, and the caller sees only that its projection would not forward.
                stoppedAtARegion = true;
                complete          = false;
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
            // The operator's `<>` arguments are ports of the same node, after the normal ones
            // (`OpDef::params` written `withAfterNorm`), and a recomputed value has to be the same
            // operator: a `softmax<1>(x)` rebuilt without its axis is `softmax(x)`, which reads the
            // last axis instead of axis 1. They are recomputed forward like the rest, because the
            // value the pullback reads must carry what the forward call carried.
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
            refuse(TransposeRefusalCause::Exit);
            os << '\n';
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
                local[ref] = accumulate(cotangentTypeOf(ref), local[ref], node);
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
                    portCotangents[i] =
                        accumulate(cotangentTypeOf(stops[i]), portCotangents[i], node);
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
                        slot       = accumulate(cotangentTypeOf(target), slot, node);
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
                        slot.cotangent = accumulate(cotangentTypeOf(target), slot.cotangent, node);
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
                    // `addFuncNode`'s last argument), one bit per argument position: the values are the
                    // captures the rule bound, the tangents are the lambda's own parameters, and the
                    // rule writes a tangent wherever its right side writes it. The rule is the one
                    // source; nothing here re-derives the split, and there is no second table to keep
                    // in step when a rule is added.
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
                                if (const gc_node_ref_t port = i < portOfParam.size()
                                                                  ? portOfParam[i]
                                                                  : rt::kInvalidNodeRef;
                                    port != rt::kInvalidNodeRef) {
                                    return port;
                                }
                                break;
                            }
                            // Not a parameter: a closure port of the half (a value the cut carried,
                            // the primal a rule reads) is a capture port of this member, and
                            // `forward` is the one entry that knows it.
                            return forward(value);
                        }
                        if (sourced->kind == rt::GCNodeKind::Data) {
                            return constantValue(value);
                        }
                        if (sourced->kind == rt::GCNodeKind::Accs) {
                            // A residual read: the forward copy answers it -- a member of the tape
                            // element, or of the tuple a parameter carries -- which is where a
                            // `split` half's per-round values are read from. Nothing else is read
                            // here: an `Accs` the forward walk cannot copy is a value this member
                            // has no other way to reach, and the arm stops by name (author's ruling
                            // of 2026-11-03, `origin/fsh-dev = ad7e716b0`: the legacy path that
                            // answered it from `paramPorts` is dropped -- it named a parameter as
                            // the holder of a value nobody had established it held, the same claim
                            // the loop's unread-state case stopped making on 2026-11-03).
                            return forward(value);
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
                    // The graph whose transposition this call calls. A kernel and a plain call are
                    // transposed as themselves; a callee that answers the pair is transposed through
                    // its second half, and that branch answers and moves on before this is read.
                    camel::runtime::GCGraph *calleeToTranspose = callee;
                    // The parameters the transposition answers for. Empty = every parameter, which is
                    // what a plain call needs; a kernel answers for its tangents.
                    std::vector<bool> participates;
                    const size_t calleeParams = callee->funcType()->normTypesCount();
                    size_t valueCount = calleeParams;
                    size_t answerCount = calleeParams;
                    // Which argument each member of the callee's answer is the cotangent of, as a
                    // position among the call's normal inputs; and which arguments are values the
                    // member reads. Both follow from the same predicate the member's signature was
                    // built from (author, 2026-11-02 11:30): an answered parameter is passed no value
                    // and answers one cotangent, and the two lists are the parameters' places.
                    std::vector<size_t> answerAt;
                    std::vector<size_t> valueAt;
                    if (tangentArgs != 0) {
                        size_t tangentCount = 0;
                        for (size_t i = 0; i < calleeParams; ++i) {
                            if (function->isTangentArgument(ref, i)) {
                                ++tangentCount;
                            }
                        }
                        os << "TRANSPOSE function=" << function->name() << " arm-call region="
                           << oldRegion << " node=" << ref << " callee=" << callee->name()
                           << " args=" << callInputs.size() << " tangent-args=" << tangentCount << '\n';
                        // A mark past the call's last argument says the record and the call do not
                        // belong together: the rule's right side put a tangent somewhere this call
                        // has no argument, and reading the rest of the marks would answer a shape the
                        // call cannot fill.
                        bool markedPastTheCall = false;
                        for (size_t i = calleeParams; i < rt::kTangentArgumentBits; ++i) {
                            markedPastTheCall = markedPastTheCall || function->isTangentArgument(ref, i);
                        }
                        if (markedPastTheCall) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref << " tangent-args=" << tangentCount
                               << " params=" << calleeParams << '\n';
                            armsOk = false;
                            return {};
                        }
                        // Every mark is the position of a tangent, wherever the rule wrote it: the
                        // answered parameters are the marked positions and the values are the rest.
                        participates.assign(calleeParams, false);
                        for (size_t i = 0; i < calleeParams; ++i) {
                            if (function->isTangentArgument(ref, i)) {
                                participates[i] = true;
                                answerAt.push_back(i);
                            } else {
                                valueAt.push_back(i);
                            }
                        }
                        valueCount  = valueAt.size();
                        answerCount = answerAt.size();
                    } else if (answersAPair(callee)) {
                        // A callee that answers the pair (value, tangent). Transposed **by its
                        // content**: the cut is made, its second half is transposed, and this call
                        // becomes a call of that transposition, closed over the values it stands on.
                        // The straight-line body applies the same rule through the same function
                        // (`transposePairCall`), so the two paths cannot answer differently; nothing
                        // here reads a record of where the callee came from (author, 2026-11-03
                        // 16:55). A hand-written graph of the same content is the same function and is
                        // answered the same way -- that is the point, not an accident.
                        PairCall pairCall;
                        pairCall.callee    = callee;
                        pairCall.ref       = ref;
                        pairCall.cotangent = local[ref];
                        for (const gc_node_ref_t argument : callInputs) {
                            pairCall.arguments.push_back(argument);
                        }
                        for (const gc_node_ref_t with : function->withInputsOf(ref)) {
                            pairCall.withArguments.push_back(with);
                        }
                        const std::function<void(gc_node_ref_t, gc_node_ref_t)> handOver =
                            [&](gc_node_ref_t target, gc_node_ref_t value) {
                                if (isConstant(target)) {
                                    return;
                                }
                                deliver(target, value, ref);
                            };
                        std::string whyPair;
                        if (!transposePairCall(
                                state, draft, pairCall, forwardInArm, handOver, os, whyPair)) {
                            os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                               << oldRegion << " node=" << ref << " pair=1 why=" << whyPair << '\n';
                            armsOk = false;
                            return {};
                        }
                        // A rule's own tangent call is taken by the branch above, which reads the
                        // positions its expansion recorded (`isTangentArgument`): the two answer
                        // different questions -- which arguments are tangents, and whether the callee
                        // answers the pair -- so neither contradicts the other.
                        continue;
                    } else {
                        // An ordinary call: a parameter whose type can hold a cotangent is answered
                        // for and is passed no value; a parameter that cannot -- the `int` of
                        // `xs[i]` -- is a value the member reads. The list is the parameters' places,
                        // so an answered parameter standing before a value is written correctly.
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
                    if (callInputs.size() != calleeParams) {
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
                    //
                    // Both facts the two former fallbacks stood for are refusals, not a missing
                    // source: `transposeSignature` answering nothing means **this callee has no
                    // transpose**, and an answer tuple whose arity is not the count this call site
                    // answers for means the signature and the marks do not belong together. A tuple
                    // of `answerCount` `float`s written in that place is a type nobody computed: it
                    // would hand the caller an `f32` cotangent to read as a `float`.
                    FunctionType *const calleeSignature = transposeSignature(
                        calleeToTranspose->funcType(), participates, nullptr,
                        &state.calleeNeeds[calleeToTranspose]);
                    Type *const calleeExit = calleeSignature != nullptr
                                                 ? calleeSignature->exitType()
                                                 : nullptr;
                    if (calleeExit == nullptr) {
                        os << "TRANSPOSE function=" << function->name()
                           << " arm-stop region=" << oldRegion << " node=" << ref
                           << " callee=" << calleeToTranspose->name() << " signature\n";
                        armsOk = false;
                        return {};
                    }
                    const auto *calleeAnswerMembers =
                        dynamic_cast<const camel::core::type::TupleType *>(calleeExit);
                    std::vector<Type *> answerComponentTypesStorage;
                    if (calleeAnswerMembers != nullptr &&
                        calleeAnswerMembers->size() == answerCount) {
                        answerComponentTypesStorage.assign(calleeAnswerMembers->types().begin(),
                                                           calleeAnswerMembers->types().end());
                    }
                    if (answerCount > 1 && answerComponentTypesStorage.size() != answerCount) {
                        // The member answers one cotangent per parameter it answers for, and this
                        // call site delivers one per marked argument: a mismatch is a record that
                        // does not describe this call, and no tuple invented here would describe it
                        // either.
                        os << "TRANSPOSE function=" << function->name()
                           << " arm-stop region=" << oldRegion << " node=" << ref
                           << " callee=" << calleeToTranspose->name() << " answers=" << answerCount
                           << " signature-members=" << answerComponentTypesStorage.size() << '\n';
                        armsOk = false;
                        return {};
                    }
                    Type *const answerType = calleeExit;
                    const gc_node_ref_t made = draft.addFuncNode(transposed, answerType);
                    std::vector<gc_node_ref_t> callArgs   = arguments;
                    callArgs.push_back(local[ref]);
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
                    for (size_t i = 0; i < answerCount; ++i) {
                        // The primal's cotangent of value i is the cotangent of the tangent that
                        // carries it; for a plain call (no lift) it is the cotangent of the argument
                        // the i-th answer belongs to, which `answerAt` states: a rule may write a
                        // tangent before a value, so the position is read and not counted.
                        const gc_node_ref_t targetAt = callInputs[answerAt[i]];
                        if (isConstant(targetAt)) {
                            // A constant argument is a known value: it has no cotangent to hand, the
                            // same way the operator rules skip a constant operand (`mul_t` does).
                            continue;
                        }
                        if (answerCount == 1) {
                            deliver(targetAt, made, ref);
                            break;
                        }
                        // The component's type is the one the signature answered for it: the
                        // two counts were compared above, so this read is the member's own type and
                        // not a `float` standing in for a member nobody looked at.
                        const gc_node_ref_t part = draft.addAccsNode(
                            answerComponentTypesStorage[i], static_cast<uint32_t>(i));
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
                // The step's cotangent space, asked once: the law is chosen at it and the rule's
                // right side is expanded at it. `hasTangentSpace` above is what makes this non-null
                // -- a value with no cotangent space never reaches the law -- so a null here is the
                // walk's own defect and not the program's.
                Type *const armCotangentType = cotangentTypeOf(ref);
                if (armCotangentType == nullptr) {
                    os << "TRANSPOSE function=" << function->name() << " arm-stop region="
                       << oldRegion << " node=" << ref << " no-cotangent-space\n";
                    armsOk = false;
                    return {};
                }
                std::string chosenRule;
                std::string ruleWhy;
                const RuleChoice armChoice = chooseTransposedRule(
                    *state.context, body->uri(), inputs, inputs.size(),
                    [&](gc_node_ref_t node) -> const Type * {
                        const camel::runtime::GCNode *at = function->node(node);
                        return at != nullptr ? at->dataType() : nullptr;
                    },
                    armCotangentType, nullptr);
                if (armChoice.rule.empty()) {
                    armsOk = false;
                    refuse(TransposeRefusalCause::Rule);
                    os << " uri=" << body->uri() << " reason=" << armChoice.why << '\n';
                    return {};
                }
                chosenRule = armChoice.rule;
                noteRule(chosenRule);
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (isConstant(inputs[i])) {
                        continue;
                    }
                    std::string why;
                    bool noInstance = false;
                    const gc_node_ref_t answer =
                        ruleCotangent(chosenRule, inputs, i, local[ref],
                                      armCotangentType, why, noInstance);
                    if (answer == rt::kInvalidNodeRef) {
                        armsOk = false;
                        // Two facts, and the expansion names which one it met: `noInstance` means the
                        // law was named and would not instantiate at these operand types -- for a
                        // body written once per rank, the ranks outside the set it serves -- and a
                        // reader acts on that by writing the missing shape's law. Every other
                        // failure is the walk failing to reach a value, which is a different edit.
                        // Naming the first as the second is how the builder's "no matching function"
                        // sentence reached a reader as though the transform had nothing to say
                        // (measured 2026-11-03 on `matmul_t`).
                        if (!noInstance) {
                            refuse(TransposeRefusalCause::CotangentNeedsAValue);
                            os << " uri=" << body->uri() << " position=" << i
                               << " reason=" << why << '\n';
                            return {};
                        }
                        refuse(TransposeRefusalCause::RuleRanks);
                        state.refusal.node  = ref;
                        state.refusal.uri   = std::string(body->uri());
                        state.refusal.rule  = chosenRule;
                        state.refusal.ranks = operandRanksText(function, inputs);
                        os << " uri=" << body->uri() << " rule=" << chosenRule
                           << " position=" << i << " ranks=" << state.refusal.ranks
                           << " operands=" << inputs.size() << " reason=" << why << '\n';
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
                        refuse(TransposeRefusalCause::Component);
                        os << " node=" << ref << " holder=" << holder << '\n';
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
                        into[j] = accumulate(cotangentTypeOf(parts2[body->value]), into[j], part);
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
                // The slot holds a **component's** cotangent, so the sum is the component's space and
                // not the tuple's: a tuple space has no addition, and asking the holder would refuse
                // a sum that is a `float`'s or an `f32`'s own. The component is the value the holder
                // was filled with at this slot when the holder is a Fill, and the tuple type's member
                // when it is a value of tuple type the body received rather than built.
                const camel::runtime::GCNode *holderNode = function->node(holder);
                const auto holderParts                  = function->withInputsOf(holder);
                Type *slotSpace                         = nullptr;
                if (holderNode != nullptr && holderNode->kind == rt::GCNodeKind::Fill &&
                    body->value < holderParts.size()) {
                    slotSpace = cotangentTypeOf(holderParts[body->value]);
                } else if (const auto *holderTuple =
                               holderNode != nullptr
                                   ? dynamic_cast<const camel::core::type::TupleType *>(
                                         holderNode->dataType())
                                   : nullptr;
                           holderTuple != nullptr && body->value < holderTuple->size()) {
                    slotSpace = cotangentTypeOfType(holderTuple->types()[body->value]);
                }
                if (slotSpace == nullptr) {
                    refuse(TransposeRefusalCause::Component);
                    os << " node=" << ref << " holder=" << holder << " slot=" << body->value
                       << '\n';
                    return false;
                }
                gc_node_ref_t &slot = slots[body->value];
                slot = accumulate(slotSpace, slot, cotangent[ref]);
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
                    if (part != rt::kInvalidNodeRef) {
                        addCotangent(parts[i], part);
                        continue;
                    }
                    // The zero of a slot nobody read is the zero of that component's own cotangent
                    // space, read from the component's type (`Tangent<f32>` is `f32`) and built by
                    // that space's own zero (`zeroOfSpaceOf`): a component with no cotangent space
                    // has no zero to write, so the slot is refused rather than handed a `float`.
                    Type *const componentType = cotangentTypeOf(parts[i]);
                    const gc_node_ref_t zero   = zeroOfSpaceOf(
                        draft, componentType,
                        parts[i] < forwardOf.size() ? forwardOf[parts[i]] : rt::kInvalidNodeRef);
                    if (componentType == nullptr || zero == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " component-zero node="
                           << parts[i] << " slot=" << i << " no-cotangent-zero\n";
                        complete = false;
                        break;
                    }
                    addCotangent(parts[i], zero);
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
                    refuse(TransposeRefusalCause::Join);
                    os << " arms=" << arms.size() << '\n';
                    return false;
                }
                // The join carries whichever arm's cotangent came back, so its value type is the
                // cotangent of the value it joins -- read from the source node, which is the value
                // both arms are cotangents of. A join created as `float` and then fed one `f32` arm
                // and one `f32` arm breaks the graph's own edge invariant ("arm 0 yields a f32, the
                // JOIN is a float"), which is a graph the run cannot carry.
                Type *const joinType = cotangentTypeOf(ref);
                if (joinType == nullptr) {
                    refuse(TransposeRefusalCause::Join);
                    os << " node=" << ref << " no-cotangent-space\n";
                    return false;
                }
                const gc_node_ref_t join = draft.addJoinNode(
                    joinType, rt::kInvalidNodeRef, static_cast<rt::gc_cnt_t>(arms.size()));
                const gc_node_ref_t brch = draft.addBrchNode(
                    Type::Int64(), join, static_cast<rt::gc_cnt_t>(arms.size()));
                // The join's norm input is the branch; the branch's own norm input is the
                // condition this arm was chosen by.
                const auto conditionInputs = function->normInputsOf(normBranch.front());
                if (conditionInputs.empty()) {
                    refuse(TransposeRefusalCause::Condition);
                    os << '\n';
                    return false;
                }
                const gc_node_ref_t condition = forward(conditionInputs.front());
                if (!complete || condition == rt::kInvalidNodeRef) {
                    refuse(TransposeRefusalCause::Condition);
                    os << '\n';
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
                        refuse(TransposeRefusalCause::ArmTangents);
                        os << " ports=" << stopPorts.size() << '\n';
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
                        refuse(TransposeRefusalCause::ArmTangents);
                        os << " ports=" << stopPorts.size() << '\n';
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
                        refuse(TransposeRefusalCause::Arm);
                        os << " arm=" << k << '\n';
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
                // the shape every arity has (author, 2026-11-02 11:30). The member type is the one
                // the join was typed with above: asking the source graph for it by a *draft* ref
                // would read whatever source node sits at that index, which is a different value.
                result = tupleOf({join}, {joinType}).first;
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
                const auto candidates        = camel::core::transposeRulesOf(std::string(uri));
                if (candidates.empty()) {
                    const auto rule = camel::core::transposeRuleOf(std::string(uri));
                    const char *reason =
                        !rule.has_value() ? "unbound"
                        : rule->status == camel::parse::ast::RuleLookup::NoSuchFamily
                            ? "no-such-family"
                            : "no-rule";
                    refuse(TransposeRefusalCause::Rule);
                    // The where of this refusal, recorded for the sentence as well as the log: the
                    // step is this node, its operator is this uri, and the lookup answered this
                    // word. The site has all three -- the log has printed them since the walk
                    // existed -- and until now the sentence a reader was handed dropped them and
                    // said only that *a* step had no rule.
                    state.refusal.node   = ref;
                    state.refusal.uri    = std::string(uri);
                    state.refusal.lookup = reason;
                    os << " uri=" << uri << " reason=" << reason << '\n';
                    return false;
                }
                const auto normOperands = function->normInputsOf(ref);
                // An operator is walked at whatever operands it has, and the *law* says which of them
                // are positions a cotangent is answered for: `mean_axis(xs, axis, keepdims)` has three
                // ports, the law names all three, and the last two answer nothing because `int` and
                // `bool` have no tangent space (`tangentSpace.h`). The count that stood here --
                // `normOperands.size() != 2 && != 1`, which refused every three-operand step before
                // the law was looked up -- was written by `a64136b6a`, when the walk knew unary and
                // binary primitives and nothing else, and `439d800be` gave the walk the node's port
                // list in the same change that wrote `mean_axis_t`. It refused a shape the law already
                // described.
                //
                // What it was protecting against is a law whose right side reads a **forward value the
                // call site cannot hand it** -- an intermediate node's, or a parameter's whose type
                // states no shape. That is now answered where it happens and refused by name there
                // (`parameter-shape`, and the port-count guard in `buildTransposeGraph`), so the count
                // is gone rather than merely loosened: a count is not the fact, and a reader told
                // "arity" learns nothing about what to change.
                // The list the rule's parameters line up with is the node's *port* list: the normal
                // operands, then the `<>` ones (`OpDef::params` written `withAfterNorm`). A rule for
                // an operator that takes its axis or its target shape in `<>` position declares that
                // argument beside the value it differentiates, so the cotangent is asked for over the
                // same list -- and the context positions answer no cotangent (they are constants with
                // no tangent space), which the loop below already says of them.
                std::vector<gc_node_ref_t> inputs(normOperands.begin(), normOperands.end());
                for (gc_node_ref_t input : function->withInputsOf(ref)) {
                    inputs.push_back(input);
                }
                // The rule the operands chose was written for a shape, and the index does not read
                // shapes: the operand types say whether this node is that shape before the right
                // side is expanded at a type tuple it was not written for. The list of positions the
                // walk will answer -- a constant, a value with no tangent space -- goes with the
                // question (`chooseRuleAmong`'s `answered`), because a position the law is never
                // handed cannot make the law wrong.
                const gc_node_ref_t ct = cotangent[ref];
                // The step's cotangent space, asked once and asked of the node's own type: the law
                // is chosen at it and the right side is expanded at it. A node that holds a
                // cotangent has a tangent space (`addCotangent` asked), so a null here is the
                // walk's own defect and not the program's -- refused rather than answered with a
                // `float` the law would then be instantiated at.
                Type *const stepCotangentType = cotangentTypeOf(ref);
                if (stepCotangentType == nullptr) {
                    os << "TRANSPOSE function=" << function->name() << " node=" << ref
                       << " no-cotangent-space\n";
                    refuse(TransposeRefusalCause::Signature);
                    return false;
                }
                std::vector<Type *> operandTypes;
                std::vector<bool> answered;
                operandTypes.reserve(inputs.size());
                answered.reserve(inputs.size());
                for (const gc_node_ref_t input : inputs) {
                    const camel::runtime::GCNode *inputNode = function->node(input);
                    operandTypes.push_back(
                        inputNode != nullptr ? const_cast<Type *>(inputNode->dataType()) : nullptr);
                    answered.push_back(
                        !isConstant(input) && wantsCotangent(input) && hasTangentSpace(input));
                }
                const RuleChoice choice = chooseTransposedRule(
                    *state.context, uri, inputs, normOperands.size(),
                    [&](gc_node_ref_t node) -> const Type * {
                        const camel::runtime::GCNode *at = function->node(node);
                        return at != nullptr ? at->dataType() : nullptr;
                    },
                    stepCotangentType, &answered);
                if (choice.rule.empty()) {
                    // The key has a law and the law was written for another shape: not `Rule`. That
                    // sentence ("the rule table has no rule for a step of this body") describes a
                    // lookup that found nothing, and this lookup found a law that says this step is
                    // not what it takes. The class comes from the choice (`RuleRefusal::Shape`),
                    // not from reading the sentence back: a family that had no word for the
                    // difference wrote one class for both, and the case that pins it
                    // (`showcase_s2`, `matmul_t.vector_operand_is_refused_by_shape`) exists because
                    // the difference tells a reader which layer owes the fix. The three facts a
                    // reader needs -- which node, which operator, and what the two type lists are --
                    // go into the record the sentence is rendered from, and the log says them here
                    // so the reading above can be grepped to the same node.
                    if (choice.refusal == RuleRefusal::Shape) {
                        // The sentence this family has always written for the class: "the rule"
                        // (the index entry a reader can go and look up), the law's own parameter
                        // list, and the operands it was offered. Built here and not inherited from
                        // `choice.why`, whose wording is the shared one ("the law takes ...") and
                        // whose tail counts the data operands -- a count that belongs to the choice
                        // and not to this refusal.
                        const std::string shape = "the rule takes " + typeTextOf(choice.lawPorts) +
                                                  ", and the operator has " +
                                                  typeTextOf(choice.operandTypes);
                        refuse(TransposeRefusalCause::RuleShape);
                        state.refusal.node  = ref;
                        state.refusal.uri   = uri;
                        state.refusal.shape = shape;
                        os << " uri=" << uri << " node=" << ref
                           << " operands=" << normOperands.size() << " reason=" << shape << '\n';
                        return false;
                    }
                    refuse(TransposeRefusalCause::Rule);
                    os << " uri=" << uri << " reason=" << choice.why << '\n';
                    return false;
                }
                noteRule(choice.rule);
                const std::string &chosenRule = choice.rule;
                // The choice refused nothing; this asks the same question at the types the
                // expansion will really build the right side from -- the choice decides *which*
                // law answers, at the instance it is chosen at, and this says whether the law it
                // picked is the shape the node is at the types the node has. Same predicate,
                // second instantiation: one criterion, not two classifications.
                {
                    std::string whyNot;
                    if (state.context != nullptr &&
                        !ruleTakesOperands(*state.context, chosenRule, operandTypes, answered,
                                           whyNot)) {
                        refuse(TransposeRefusalCause::RuleShape);
                        state.refusal.node  = ref;
                        state.refusal.uri   = uri;
                        state.refusal.shape = whyNot;
                        os << " uri=" << uri << " node=" << ref
                           << " operands=" << normOperands.size() << " reason=" << whyNot << '\n';
                        return false;
                    }
                }
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
                    bool noInstance = false;
                    const gc_node_ref_t answer = ruleCotangent(
                        chosenRule, inputs, i, ct, stepCotangentType, why,
                        noInstance);
                    if (answer == rt::kInvalidNodeRef) {
                        // The same two facts as the arm walk above, told apart the same way: the law
                        // would not instantiate at these operand types, or the walk could not reach a
                        // value the answer reads. The rank is printed for the first because it is the
                        // fact a reader edits, and the law's name because it is the function to edit.
                        if (!noInstance) {
                            refuse(TransposeRefusalCause::CotangentNeedsAValue);
                            os << " uri=" << uri << " position=" << i << " reason=" << why
                               << '\n';
                            return false;
                        }
                        refuse(TransposeRefusalCause::RuleRanks);
                        state.refusal.node  = ref;
                        state.refusal.uri   = uri;
                        state.refusal.rule  = chosenRule;
                        state.refusal.ranks = operandRanksText(function, inputs);
                        os << " uri=" << uri << " rule=" << chosenRule << " node=" << ref
                           << " position=" << i << " ranks=" << state.refusal.ranks
                           << " operands=" << inputs.size() << " reason=" << why << '\n';
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
                    refuse(TransposeRefusalCause::State);
                    os << " node=" << ref << " origin=" << regionOrigin << '\n';
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
                // **The rounds are undone from the last to the first** (design 4.2, 4.3). Copying
                // the sequence into the reverse loop and iterating it -- what this case did -- walks
                // the rounds in the order they were written, so a per-round map that does not commute
                // comes back as the product in the wrong order: `vjp_loop.rounds_are_undone_in_reverse`
                // reads (1.0, 10.0) that way and (1.0, 1.0) this way. The reverse loop therefore
                // iterates `range(n - 1, -1, -1)` -- the round *numbers*, the last one first -- and a
                // round reads its own element out of the sequence with `:op/idx_arr`, the operator
                // `xs[i]` is built from (`index_into` in `src/core/forward.cpp`); the reverse pass in
                // `modules/autodiff/pullback.cpp` reads its tape the same way. The element is no
                // longer a value the two loops share: it is a function of the round number, which is
                // what makes the pairing right rather than merely reversed.
                const bool iterates    = camel::core::loopHasIterable(function->loopKindOf(ref));
                // Where the round takes its element from: the sequence itself (an array) or the two
                // bounds of an interval, from which the round takes `a + round`
                // (`LoopIterableForm`, include/camel/core/loop_kind.h). Both are read outside the
                // loop, so the reverse loop's own body only carries the round number.
                gc_node_ref_t sequence = rt::kInvalidNodeRef;
                gc_node_ref_t intervalLower = rt::kInvalidNodeRef;
                gc_node_ref_t backwardsRange = rt::kInvalidNodeRef;
                bool intervalForm = false;
                if (iterates) {
                    sequence = oldNorm.empty() ? rt::kInvalidNodeRef : forward(oldNorm.front());
                    if (sequence == rt::kInvalidNodeRef) {
                        refuse(TransposeRefusalCause::Range);
                        os << " node=" << ref << '\n';
                        return false;
                    }
                    intervalForm =
                        function->loopIterableFormOf(ref) == camel::core::LoopIterableForm::Interval;
                    const auto intConstant = [&](int64_t value) {
                        return draft.materializeStaticValue(
                            camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(value),
                            Type::Int64(),
                            rt::kGCNodeFlagConstant);
                    };
                    // How many rounds the loop runs: the sequence's length, or `b - a` for an
                    // interval. An empty sequence makes an empty range, and the loop then runs no
                    // round -- which is the same number of rounds the forward loop ran.
                    gc_node_ref_t roundCount = rt::kInvalidNodeRef;
                    if (intervalForm) {
                        const std::array<gc_node_ref_t, 1> pair{sequence};
                        intervalLower = draft.addAccsNode(Type::Int64(), 0);
                        draft.setNormInputs(
                            intervalLower, std::span<const gc_node_ref_t>(pair.data(), pair.size()));
                        const gc_node_ref_t upper = draft.addAccsNode(Type::Int64(), 1);
                        draft.setNormInputs(
                            upper, std::span<const gc_node_ref_t>(pair.data(), pair.size()));
                        const std::array<gc_node_ref_t, 2> span{upper, intervalLower};
                        roundCount = draft.addOperNode(Type::Int64(), nullptr, ":op/sub_l");
                        draft.setNormInputs(
                            roundCount, std::span<const gc_node_ref_t>(span.data(), span.size()));
                    } else {
                        const std::array<gc_node_ref_t, 1> only{sequence};
                        roundCount = draft.addOperNode(Type::Int64(), nullptr, ":struct/len_arr");
                        draft.setNormInputs(
                            roundCount, std::span<const gc_node_ref_t>(only.data(), only.size()));
                    }
                    const std::array<gc_node_ref_t, 2> lastRound{roundCount, intConstant(1)};
                    const gc_node_ref_t lastIndex =
                        draft.addOperNode(Type::Int64(), nullptr, ":op/sub_l");
                    draft.setNormInputs(
                        lastIndex, std::span<const gc_node_ref_t>(lastRound.data(), lastRound.size()));
                    const std::array<gc_node_ref_t, 3> steps{lastIndex, intConstant(-1), intConstant(-1)};
                    Type *const rangeType = ArrayType::create(Type::Int64());
                    backwardsRange = draft.addOperNode(rangeType, nullptr, ":struct/range");
                    draft.setNormInputs(
                        backwardsRange, std::span<const gc_node_ref_t>(steps.data(), steps.size()));
                }
                const uint32_t newRegion =
                    draft.addRegion(rt::GCRegionKind::LoopBody, 0);
                draft.setInsertRegion(newRegion);
                // The body takes the round number first and the states after it; each state becomes
                // one cotangent, so the region grows one port per state and, later, one per escaped
                // value. The number is not differentiated (it comes from the range), so it keeps its
                // type and goes unused by the rules.
                std::vector<gc_node_ref_t> newPorts;
                Type *const elementType =
                    iterates ? Type::Int64() : function->node(regionPorts.front())->dataType();
                const gc_node_ref_t elementPort =
                    draft.addPortNode(elementType, draft.allocateRuntimeSlot(elementType));
                newPorts.push_back(elementPort);
                // The round's element, read at the round's own number: `xs[round]` for an array and
                // `a + round` for an interval. Both stand inside the body region, so the number they
                // read is this round's. A rule that needs the round's own state still refuses
                // (`skip=state`): the state has no per-round value in the reverse walk.
                if (iterates) {
                    const gc_node_ref_t elementOfRound = [&]() -> gc_node_ref_t {
                        if (intervalForm) {
                            const std::array<gc_node_ref_t, 2> at{intervalLower, elementPort};
                            const gc_node_ref_t sum =
                                draft.addOperNode(Type::Int64(), nullptr, ":op/add_l");
                            draft.setNormInputs(
                                sum, std::span<const gc_node_ref_t>(at.data(), at.size()));
                            return sum;
                        }
                        const std::array<gc_node_ref_t, 2> at{sequence, elementPort};
                        const gc_node_ref_t read = draft.addOperNode(
                            function->node(regionPorts.front())->dataType(), nullptr, ":op/idx_arr");
                        draft.setNormInputs(
                            read, std::span<const gc_node_ref_t>(at.data(), at.size()));
                        return read;
                    }();
                    forwardOf[regionPorts.front()] = elementOfRound;
                } else {
                    forwardOf[regionPorts.front()] = elementPort;
                }
                // A state's cotangent port lives in the state's own tangent space: a loop that
                // carries a `float[]` state pulls back an array, and a port typed `float` would hand
                // that array to a number -- the same read that took the process down when two
                // elements of one array were summed (design principles 9.9).
                std::vector<gc_node_ref_t> ctPorts;
                std::vector<Type *> stateCotangentTypes;
                stateCotangentTypes.reserve(stateCount);
                for (size_t i = 0; i < stateCount; ++i) {
                    const size_t portIndex = 1 + i;
                    // The state's cotangent space, read from the state's own port. A state port
                    // this loop does not have is a count that disagrees with the region's ports --
                    // the walk's own defect, not the program's -- so it refuses rather than typing
                    // the port `float`.
                    Type *const type =
                        portIndex < regionPorts.size() ? cotangentTypeOf(regionPorts[portIndex])
                                                       : nullptr;
                    if (type == nullptr) {
                        os << "TRANSPOSE function=" << function->name() << " node=" << ref
                           << " state=" << i << " no-cotangent-space\n";
                        refuse(TransposeRefusalCause::State);
                        return false;
                    }
                    stateCotangentTypes.push_back(type);
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
                        refuse(TransposeRefusalCause::State);
                        os << " node=" << ref << " origin=" << regionOrigin
                           << " captures=" << caught.size() << '\n';
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
                    refuse(TransposeRefusalCause::State);
                    os << " node=" << ref << " origin=" << regionOrigin << '\n';
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
                        part == rt::kInvalidNodeRef && 1 + i < regionPorts.size()
                            ? zeroOf(regionPorts[1 + i])
                            : part);
                    componentTypes.push_back(stateCotangentTypes[i]);
                }
                for (const auto &entry : caught) {
                    // The accumulator is the escaped value's own cotangent, so its port is typed by
                    // that value's tangent space -- the same answer the state ports above ask. A
                    // `float` port holding an array was this site's lie: `:op/add_d` on an array
                    // reads it as a number.
                    Type *const accType = cotangentTypeOf(entry.first);
                    if (accType == nullptr) {
                        os << "TRANSPOSE function=" << function->name() << " node=" << ref
                           << " escape=" << entry.first << " no-cotangent-space\n";
                        refuse(TransposeRefusalCause::State);
                        return false;
                    }
                    const gc_node_ref_t accPort =
                        draft.addPortNode(accType, draft.allocateRuntimeSlot(accType));
                    newPorts.push_back(accPort);
                    // The round adds what escaped to the sum the round before handed on. One
                    // accumulator per value, not per product: the products that call one value
                    // add into the same sum when they are delivered (the collection above). The sum
                    // is the type's own: `:tangent/add_arr` for an array, the scalar's own addition
                    // for a number.
                    regionParts.push_back(accumulate(accType, accPort, entry.second.cotangent));
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
                    [&] {
                        rt::GCLoopBody payload = rt::loopBodyOf(*function, ref);
                        if (iterates) {
                            // The reverse loop iterates the round numbers, which is an array
                            // whatever the old loop's sequence was: an interval loop's pair is read
                            // by the body through `a + round`, and the numbers come from an array.
                            payload.iterableForm = camel::core::LoopIterableForm::Array;
                        }
                        return payload;
                    }(),
                    loopType);
                draft.setRegionOwner(newRegion, loopNode, function->region(bodyRegionOld)->index);
                draft.setNodeOrigin(loopNode, static_cast<uint32_t>(function->region(bodyRegionOld)->origin));
                std::vector<gc_node_ref_t> loopNorm;
                // What the loop iterates is `range(n - 1, -1, -1)`: the reverse walk of the rounds,
                // not the sequence in the order it was written. Everything before the states in the
                // old norm list is that sequence, and it is now a value the *body* reads at the round
                // number instead, so it is not a norm input of this loop at all.
                if (iterates) {
                    loopNorm.push_back(backwardsRange);
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
                    // A slot nobody read is zero: that state's value pulls nothing back into it. The
                    // zero is the zero of the state's own cotangent space, which is the type the port
                    // above was made at -- an `f32` state's seed is an `f32` zero, built by that
                    // space's own zero and not by a `float` constant.
                    loopNorm.push_back(
                        slot == rt::kInvalidNodeRef
                            ? zeroOfSpaceOf(draft, stateCotangentTypes[i], rt::kInvalidNodeRef)
                            : slot);
                }
                for (size_t i = 0; i < caught.size(); ++i) {
                    // Every sum starts at zero: a value a round never reaches adds nothing. The sum
                    // is a component of the loop's value like any other, so its zero is the zero of
                    // that component's own cotangent space (`componentTypes`, which the answer's
                    // components were typed from).
                    const size_t componentAt = stateCount + i;
                    loopNorm.push_back(zeroOfSpaceOf(
                        draft, componentAt < componentTypes.size() ? componentTypes[componentAt]
                                                                   : nullptr,
                        rt::kInvalidNodeRef));
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
                        // is an array, and reading it as a float is the same read `add_d` made. A
                        // component index this region has no type for is a count that disagrees
                        // with the parts the region returns -- the walk's own defect -- so it
                        // answers no node rather than a `float` read.
                        Type *const componentType =
                            index < componentTypes.size() ? componentTypes[index] : nullptr;
                        if (componentType == nullptr) {
                            return rt::kInvalidNodeRef;
                        }
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
                    // is the transposed function's whole answer **when the value the state starts from
                    // is a parameter** -- the hand-written loop `thrice(x) = loop s = x for i in 0..3`
                    // is the shape, and there one cotangent, in the one-slot tuple the signature
                    // promises, is exactly right (author, 2026-11-02 11:30; handing the scalar out
                    // unwrapped is what took the process down when the graph was called).
                    //
                    // A state that starts at anything else answers no parameter, and the loop's
                    // cotangent then has nowhere to land: the backward half `split` hands out starts
                    // its tangent at the constant the lifted graph wrote, so its parameters are the
                    // tangents the escapes carry, and a half that escaped nothing is a function no
                    // parameter reaches. Answering the loop there named a cotangent for a value no
                    // parameter is -- `h(x) = loop s = 0.0 for i in [1,2,3,4] do s + 2.0` answered
                    // `1.0` for a derivative of `0.0` -- so the loop is not the answer. The reference
                    // answers zero instead, which is the same reading the straight-line case took on
                    // 48bfa6778 ("a parameter the body never reads answers a zero cotangent, not a
                    // refusal", author's ruling of 2026-11-03, `origin/fsh-dev = ad7e716b0`, fourth
                    // of four): the parameter is not read, so it has no influence, so its cotangent is
                    // zero. The answer is therefore built from the parameters the walk reached, the
                    // way every other transposition builds it, and a parameter nothing delivered to
                    // keeps the zero the tail already writes for it.
                    if (stateInitials[0] != rt::kInvalidNodeRef) {
                        addCotangent(stateInitials[0], loopNode);
                    }
                    const camel::runtime::GCNode *const start =
                        stateInitials[0] < nodeCount ? function->node(stateInitials[0]) : nullptr;
                    if (start != nullptr && start->kind == rt::GCNodeKind::Port) {
                        // The member type is the loop's own (`loopType`: the state's cotangent
                        // space, or the tuple of the states' and the sums' spaces). Asking the
                        // *source* graph for it by the draft's ref would read whatever source node
                        // sits at that index -- a different value with a type of its own.
                        result = tupleOf({loopNode}, {loopType}).first;
                    } else {
                        // Nothing reaches that value, so nothing the loop did can reach a parameter,
                        // so the loop answers the zero of its cotangent space: the same reading the
                        // straight-line case took on 48bfa6778 ("a parameter the body never reads
                        // answers a zero cotangent, not a refusal"), read for a loop. A member
                        // carries its closure whatever its answer reads -- a zero answer is a
                        // constant and the closure is still filled -- so there is no refusal here
                        // (author, 2026-11-03, `origin/fsh-dev = 3ab482ab2`). The test is no longer
                        // whether the member carries a closure (`closureOf`), which would refuse the
                        // half `vjp` hands out: that half carries the loop's tape and is exactly the
                        // shape the zero is for.
                        //
                        // The constant is not written here. `result` stays invalid, and the answer is
                        // built by the walk below exactly as every other transposition builds it: one
                        // value per parameter the walk reached, each read from that parameter's own
                        // cotangent slot, wrapped in the tuple the signature promises (`(float,)` for
                        // one parameter; author, 2026-11-02 11:30). A constant written here instead
                        // took the *loop's* cotangent type for the answer's: the slot holds the
                        // answer's type, so the value has to be the answer's too -- a bare scalar in a
                        // slot or a tuple on its way into a tuple is what took the process down on
                        // 843d38025, and the loop's cotangent type is not even a question this graph
                        // can answer (the ref is a node of the draft, the types belong to the source).
                    }
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
                    refuse(TransposeRefusalCause::Kind);
                    os << " node=" << ref << " kind=" << static_cast<int>(node->kind)
                       << '\n';
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
                    refuse(TransposeRefusalCause::CallNoTranspose);
                    os << " node=" << ref << " callee=none value="
                       << describe(inputs.front());
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
                // arguments the rule marked as tangents is transposed as the linear map it is, and its
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
                    refuse(TransposeRefusalCause::CallNoTranspose);
                    os << " node=" << ref << " callee=" << callee->name()
                       << " params=" << calleeParams << '\n';
                    return false;
                }
                std::vector<size_t> answersOf;
                // A **rule kernel** call is linear in its tangents, and which arguments those are is
                // the rule's own fact, recorded on this node when `reinterp` expanded it
                // (`GCNode::tangentArgs`, one bit per argument): the values are the captures the rule
                // bound, the tangents are the lambda's parameters. The positions are read and not
                // counted -- a rule may write a tangent before a value (`index_into(dxs, i)`), and a
                // run of trailing ones would call that call's value a tangent. The arm path has read
                // the record since it existed; the straight-line body reads it here for the same
                // reason -- a closed half's body is nothing but such calls, and a tangent has no value
                // to forward.
                const uint16_t tangentMask = function->callTangentArgsOf(ref);
                std::vector<bool> participates;
                if (tangentMask != 0) {
                    size_t tangentCount = 0;
                    bool markedPastTheCall = false;
                    for (size_t i = 0; i < calleeParams; ++i) {
                        if (function->isTangentArgument(ref, i)) {
                            ++tangentCount;
                        }
                    }
                    for (size_t i = calleeParams; i < rt::kTangentArgumentBits; ++i) {
                        markedPastTheCall = markedPastTheCall || function->isTangentArgument(ref, i);
                    }
                    if (markedPastTheCall) {
                        refuse(TransposeRefusalCause::CallNoTranspose);
                        os << " node=" << ref << " callee=" << callee->name()
                           << " tangent-args=" << tangentCount << " params=" << calleeParams
                           << '\n';
                        return false;
                    }
                    participates.assign(calleeParams, false);
                    for (size_t i = 0; i < calleeParams; ++i) {
                        if (function->isTangentArgument(ref, i)) {
                            participates[i] = true;
                        }
                    }
                }
                // A callee that answers the pair (value, tangent). **The same rule the arm path
                // applies** -- cut the callee by its content, transpose the second half, turn this
                // call into a call of that transposition -- through the same function, so a
                // straight-line body and an arm cannot answer the same shape differently. A
                // hand-written graph of the same content is answered here exactly as a lifted one is
                // (author, 2026-11-03 16:55).
                if (tangentMask == 0 && answersAPair(callee)) {
                    // The cotangent this call's **tangent component** carries. The pair is read a
                    // component at a time, so the cotangent is the tuple's slot 1 -- the slot the
                    // component read put it in (`tupleCotangents`), which is what the component read
                    // above stands for. A call whose answer is the pair *as a whole* has no such slot:
                    // this step has no rule for a cotangent arriving on a pair, and saying so is the
                    // refusal rather than reading one component's cotangent as another's.
                    gc_node_ref_t pairCotangent = rt::kInvalidNodeRef;
                    if (const auto slots = tupleCotangents.find(ref);
                        slots != tupleCotangents.end() && slots->second.size() > 1) {
                        pairCotangent = slots->second[1];
                    }
                    if (pairCotangent == rt::kInvalidNodeRef) {
                        refuse(TransposeRefusalCause::CotangentNeedsAValue);
                        os << " node=" << ref << " callee=" << callee->name()
                           << " pair=1 why=no-cotangent-on-the-tangent-component\n";
                        return false;
                    }
                    PairCall pairCall;
                    pairCall.callee    = callee;
                    pairCall.ref       = ref;
                    pairCall.cotangent = pairCotangent;
                    for (size_t i = 0; i < calleeParams; ++i) {
                        pairCall.arguments.push_back(inputs[argumentAt + i]);
                    }
                    for (const gc_node_ref_t with : function->withInputsOf(ref)) {
                        pairCall.withArguments.push_back(with);
                    }
                    const std::function<void(gc_node_ref_t, gc_node_ref_t)> handOver =
                        [&](gc_node_ref_t target, gc_node_ref_t value) {
                            addCotangent(target, value);
                        };
                    std::string whyPair;
                    if (!transposePairCall(
                            state, draft, pairCall, forward, handOver, os, whyPair)) {
                        refuse(TransposeRefusalCause::CallNoTranspose);
                        os << " node=" << ref << " callee=" << callee->name()
                           << " pair=1 why=" << whyPair << '\n';
                        return false;
                    }
                    break;
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
                            refuse(TransposeRefusalCause::CallNoTranspose);
                            os << " node=" << ref << " callee=" << callee->name()
                               << '\n';
                            return false;
                        }
                    } else {
                        transposed = known->second;
                    }
                }
                FunctionType *calleeType = transposeSignature(
                    callee->funcType(), participates, &answersOf, &state.calleeNeeds[callee]);
                if (calleeType == nullptr || calleeType->normTypesCount() == 0) {
                    refuse(TransposeRefusalCause::CallNoTranspose);
                    os << " node=" << ref << " callee=" << callee->name()
                       << " signature\n";
                    return false;
                }
                // The member's value parameters are the callee's parameters it answers nothing for, in
                // the order they stand, so the call hands one forwarded value per such parameter.
                // `xs[i]` is why this is read from the same predicate the member's signature was
                // built from: one value per *leading* parameter would hand it the array where the
                // index belongs, and `(xs, i)` is the shape where an answered parameter stands before
                // a value.
                const size_t memberValues = calleeType->normTypesCount() - 1;
                // Which of the two shapes a value this call cannot hand over is. The call site has
                // one diagnostic for both, and until 2026-11-03 one word as well, so a refusal that
                // meant the second read like the first and looked fixed:
                //
                // - the value is the **parameter's own**, and this call does not carry it (the shape
                //   the membership test above answers, and the one the array cases left behind);
                // - the value is one this **half's body computes** -- a loop's result, a branch's --
                //   and it is not missing from the call at all: it stands in this graph, and handing
                //   it over means copying a node whose value comes out of a region, which `forward`
                //   cannot do (it copies Data, Port, Accs, Fill and Oper, and stops at `Loop`).
                //   `vjp_loop.rounds_are_undone_in_reverse` is that shape: the call is
                //   `index_into(dxs, 0)` and `dxs` is the backward half's own loop.
                //
                // The node's kind is the whole of the difference, so it is read and not guessed: a
                // value that comes out of a region is one the walk would have to transpose the
                // region for, and a value that does not is one the call or its closure owes.
                const auto unforwardedCause = [&](gc_node_ref_t ref, size_t param) {
                    // Ask again with the flag cleared, so the answer is this attempt's: a failure is
                    // not memoised, so the walk repeats the same path and stops at the same node.
                    stoppedAtARegion          = false;
                    const gc_node_ref_t probe = forward(ref);
                    if (probe == rt::kInvalidNodeRef && stoppedAtARegion) {
                        refuse(TransposeRefusalCause::CallValueComputedByTheBody, param);
                        return;
                    }
                    refuse(TransposeRefusalCause::CallValueMissing, param);
                };
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
                        refuse(TransposeRefusalCause::CallValueMissing, i);
                        os << " param=" << i << " node=" << ref << " arg=" << i
                           << " callee=" << callee->name() << '\n';
                        return false;
                    }
                    const gc_node_ref_t argument = forward(inputs[argumentAt + i]);
                    if (argument == rt::kInvalidNodeRef) {
                        unforwardedCause(inputs[argumentAt + i], i);
                        os << " param=" << i << " node=" << ref
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
                        refuse(TransposeRefusalCause::CallValueMissing, idx);
                        os << " param=" << idx << " node=" << ref << " arg=" << idx
                           << " callee=" << callee->name() << '\n';
                        return false;
                    }
                    const gc_node_ref_t captured = forward(inputs[argumentAt + idx]);
                    if (captured == rt::kInvalidNodeRef) {
                        unforwardedCause(inputs[argumentAt + idx], idx);
                        os << " param=" << idx << " node=" << ref
                           << " arg=" << inputs[argumentAt + idx] << '\n';
                        return false;
                    }
                    arguments.push_back(captured);
                }
                if (arguments.size() != memberValues) {
                    refuse(TransposeRefusalCause::CallNoTranspose);
                    os << " node=" << ref << " values=" << arguments.size()
                       << " member=" << memberValues << '\n';
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
                        // Written in place: a FUNC node, whose type is the call's value -- which is
                        // the member's own type `calleeType`, the same one the static function
                        // value below is written at. A `float` written here is a call whose callee
                        // value is typed `float` while the call's answer is not.
                        calleeValue = draft.addFuncNode(transposed, calleeType);
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
                refuse(TransposeRefusalCause::Kind);
                os << " node=" << ref << '\n';
                return false;
            }
        }
        if (cotangentArithmeticMissing) {
            // A value read twice whose cotangent space registers no addition: the sums it needs are
            // not a thing its type has, so this member answers no cotangent. Refused here rather
            // than at the first failed sum, because one reading of the cause is what a reader
            // greps and the walk may have gone on to place other cotangents meanwhile.
            refuse(TransposeRefusalCause::NoCotangentArithmetic);
            os << '\n';
            return false;
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
            // behalf of a type that is not one. A parameter with no tangent space answers the empty
            // member `()` -- the signature has one place per parameter, and this is that place with
            // nothing in it -- which is what keeps the tuple the signature promised and the tuple this
            // builds the same length and the same order.
            Type *const unitType = camel::core::type::TupleType::create(std::vector<Type *>{});
            const auto unit      = [&]() {
                ::Tuple *emptyTuple = ::Tuple::create(0, mm::autoSpace());
                return draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), unitType);
            };
            std::vector<Type *> partTypes;
            // The **parameters**, not the ports: the walk above says which port a parameter asks for,
            // and a `()` place has no port at all. Iterating `ports` was the same reading of a port
            // index as a parameter index that named the wrong parameter everywhere else.
            for (size_t at = firstPort; at < paramCount; ++at) {
                if (!participatesOf(at)) {
                    // A parameter the transposition answers nothing for. A `()` place owns a member
                    // of the answer and that member is empty -- `Tangent<int>` is `()` -- while an
                    // `int` parameter, which the caller still passes as a value, owns no member at
                    // all. The two are told apart by the type (`isUnitType`), the same query the
                    // signature answered with.
                    Type *const parameterType = function->funcType()->normTypeAt(at);
                    if (isUnitType(parameterType)) {
                        parts.push_back(unit());
                        partTypes.push_back(unitType);
                    }
                    continue;
                }
                const size_t port =
                    at < portOfParameter.size() ? portOfParameter[at] : paramCount;
                if (port >= ports.size()) {
                    refuse(TransposeRefusalCause::UnreadParameter);
                    os << " at=" << at << '\n';
                    return false;
                }
                const camel::runtime::GCNode *portNode = function->node(ports[port]);
                const Type *portType = portNode != nullptr ? portNode->dataType() : nullptr;
                const camel::core::TangentSpaceAnswer space =
                    portType == nullptr ? camel::core::TangentSpaceAnswer{}
                                        : camel::core::tangentSpaceOf(portType);
                if (hasCotangent[ports[port]]) {
                    parts.push_back(cotangent[ports[port]]);
                    partTypes.push_back(space.space);
                    continue;
                }
                // The zero is the parameter's own cotangent space's (`zeroOfSpaceOf`), built from the
                // value the walk forwarded for that parameter when there is one and from the type's
                // stated extents otherwise -- never from a source ref written into a node of this
                // graph. An `f32` slot is an `f32` zero; an array slot is the space's own
                // `:tangent/zeros_arr` of that array's shape.
                //
                // A parameter with no tangent space answers the empty member `()` above, so a null
                // space here is a signature that admitted a parameter nothing can answer for: a
                // refusal by name, not a `float` member the signature never promised.
                if (space.space == nullptr) {
                    refuse(TransposeRefusalCause::UnreadParameter);
                    os << " port=" << ports[port] << " at=" << at << " no-cotangent-space\n";
                    return false;
                }
                const gc_node_ref_t zero = zeroOfSpaceOf(
                    draft, space.space,
                    ports[port] < forwardOf.size() ? forwardOf[ports[port]] : rt::kInvalidNodeRef);
                if (zero == rt::kInvalidNodeRef) {
                    refuse(TransposeRefusalCause::UnreadParameter);
                    os << " port=" << ports[port]
                       << " type=" << (portType != nullptr ? portType->toString() : "?")
                       << " no-cotangent-zero\n";
                    return false;
                }
                parts.push_back(zero);
                partTypes.push_back(space.space);
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
            refuse(TransposeRefusalCause::Output);
            os << '\n';
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

bool transposePairCall(
    TransposeState &state, rt::GraphDraft &draft, const PairCall &call,
    const std::function<gc_node_ref_t(gc_node_ref_t)> &forward,
    const std::function<void(gc_node_ref_t, gc_node_ref_t)> &deliver, std::ostream &os,
    std::string &why) {
    const auto fail = [&why](std::string reason) {
        why = std::move(reason);
        return false;
    };
    // The cut. The criterion answered that the callee *is* a pair; whether the cut can be built is the
    // cut's own question, and its answer -- in its own words -- is this call's answer. It is never a
    // reason to fall back on another rule for the same call.
    SplitGraph cut = buildSplitGraph(state.context, state.program, call.callee, os);
    if (cut.forward == nullptr || cut.backward == nullptr) {
        return fail(
            cut.refusedWhy.empty()
                ? "the callee answers the pair (value, tangent) and the cut does not build it"
                : "the callee answers the pair (value, tangent) and the cut does not build it: " +
                      cut.refusedWhy);
    }
    // The values the second half reads, found at the call **by identity**: a residual that is a
    // parameter is that parameter's argument, and a residual that is a `with` port is that `with`
    // argument. A residual neither of those is a value the callee computes inside itself, which this
    // call does not stand on -- so the call is refused by name rather than handed a neighbouring
    // argument ("a positional convention would make the answer depend on what the caller wrote").
    std::vector<gc_node_ref_t> closureValues;
    for (const Residual &residual : residualsOf(*call.callee, state.program)) {
        gc_node_ref_t atCall = rt::kInvalidNodeRef;
        if (residual.parameter != Residual::kNotAParameter) {
            if (residual.parameter < call.arguments.size()) {
                atCall = call.arguments[residual.parameter];
            }
        } else {
            const std::span<const gc_node_ref_t> withPorts = call.callee->withPorts();
            for (size_t i = 0; i < withPorts.size() && i < call.withArguments.size(); ++i) {
                if (withPorts[i] == residual.node) {
                    atCall = call.withArguments[i];
                    break;
                }
            }
        }
        if (atCall == rt::kInvalidNodeRef) {
            return fail("a value the second half reads is computed inside the callee, so this call does "
                        "not stand on it");
        }
        const gc_node_ref_t placed = forward(atCall);
        if (placed == rt::kInvalidNodeRef) {
            return fail("a value the second half reads cannot be carried across this call");
        }
        closureValues.push_back(placed);
    }
    // The member: the transposition of the **second half**, built once per graph (the map the main
    // walk fills too), so two calls of one callee call one member.
    GraphId member = rt::kInvalidGraphId;
    if (const auto known = state.callees.find(rt::toValue(cut.backward->id()));
        known != state.callees.end()) {
        member = known->second;
    } else {
        member = state.group.reserve(
            "transpose_of_" + std::string(cut.backward->name()),
            "transpose_of_" + std::string(cut.backward->name()));
        state.callees.emplace(rt::toValue(cut.backward->id()), member);
        if (!buildTransposeMember(state, cut.backward, member, os, {})) {
            return fail("the second half of the callee cannot be transposed");
        }
    }
    // What that member takes and answers. `participates` is empty -- every parameter of the second half
    // is a linear input -- and it is the same query the member itself was built from, so the call
    // cannot hand it a list of a different length than its ports.
    std::vector<size_t> answersOf;
    FunctionType *const memberType = transposeSignature(
        cut.backward->funcType(), {}, &answersOf, &state.calleeNeeds[cut.backward]);
    if (memberType == nullptr || memberType->exitType() == nullptr) {
        return fail("the second half of the callee has no transposition signature");
    }
    // The values the member takes for the parameters it answers nothing for, and then the values the
    // member asked for (`calleeNeeds`), in that order: the order the signature wrote them in.
    const size_t half       = call.callee->funcType()->normTypesCount() / 2;
    const size_t memberCount = cut.backward->funcType()->normTypesCount();
    std::vector<gc_node_ref_t> arguments;
    for (size_t at = 0; at < memberCount; ++at) {
        Type *const parameter = cut.backward->funcType()->normTypeAt(at);
        const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(parameter);
        if (space.hasSpace() && space.space != nullptr) {
            continue; // answered: the caller passes no value for it
        }
        if (isUnitType(parameter)) {
            continue; // `()` is an answer with an empty slot, not a port nobody can fill
        }
        if (half + at >= call.arguments.size()) {
            return fail("the call does not carry a value the member takes");
        }
        const gc_node_ref_t placed = forward(call.arguments[half + at]);
        if (placed == rt::kInvalidNodeRef) {
            return fail("a value the member takes cannot be carried across this call");
        }
        arguments.push_back(placed);
    }
    for (const size_t at : state.calleeNeeds[cut.backward]) {
        if (half + at >= call.arguments.size()) {
            return fail("the call does not carry a value the member needs");
        }
        const gc_node_ref_t placed = forward(call.arguments[half + at]);
        if (placed == rt::kInvalidNodeRef) {
            return fail("a value the member needs cannot be carried across this call");
        }
        arguments.push_back(placed);
    }
    if (arguments.size() + 1 != memberType->normTypesCount()) {
        return fail("the values this call can hand the member are not the ones it takes");
    }
    // The call: an **indirect** call of a closure value. The value is the second half's own graph
    // closed over the values above -- the writer the cut uses for `lin`, so a closure a call site
    // makes and one the cut makes are the same value -- and the runtime fills the member's closure
    // ports from it (`populateIndirectCallFrame`: `with`-arg 0 is the function).
    const gc_node_ref_t calleeValue = buildLinearClosure(
        draft, state.program, member, cut.backward->closureType(), cut.backward->funcType(),
        std::span<const gc_node_ref_t>(closureValues.data(), closureValues.size()));
    std::vector<gc_node_ref_t> callArgs = arguments;
    callArgs.push_back(call.cotangent);
    Type *const answerType = memberType->exitType();
    const gc_node_ref_t made = draft.addCallNode(answerType, rt::GCCallBody{});
    draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
    draft.setWithInputs(made, std::span<const gc_node_ref_t>(&calleeValue, size_t(1)));
    draft.addDependency(cut.backward->id());
    // The answers: one cotangent per parameter of the second half, in the member's order. Each one is
    // the cotangent of the tangent that stands in the same place at the call -- the second half's
    // parameter `k` is the callee's parameter `half + k`, which is the argument the call passed.
    const auto *answerTuple =
        answerType->code() == camel::core::type::TypeCode::Tuple
            ? tt::as_ptr<camel::core::type::TupleType>(answerType)
            : nullptr;
    for (size_t k = 0; k < answersOf.size(); ++k) {
        if (half + answersOf[k] >= call.arguments.size()) {
            continue;
        }
        if (answerTuple == nullptr) {
            deliver(call.arguments[half + answersOf[k]], made);
            continue;
        }
        const gc_node_ref_t part =
            draft.addAccsNode(answerTuple->typeAt(k), static_cast<uint32_t>(k));
        draft.setNormInputs(part, std::span<const gc_node_ref_t>(&made, 1));
        deliver(call.arguments[half + answersOf[k]], part);
    }
    return true;
}

TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os, bool promisedToTheOperator) {
    if (program == nullptr || function == nullptr) {
        return TransposeGraph{};
    }
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "transpose_of_" + std::string(function->name()),
        "transpose_of_" + std::string(function->name()));
    TransposeState state{group, program, {}, context};
    if (!buildTransposeMember(state, function, member, os)) {
        // The cause the walk recorded, not a reading of the log: a null graph and the reason it is
        // null are one answer, handed back together.
        return TransposeGraph{nullptr, nullptr, state.refusal};
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    // The graph must take exactly what the operator's own type says it takes. `transpose(f)` is
    // resolved by `transposeOperatorSignature`, which reads `f`'s type and no body, so the call site
    // is told `(ct) => (cotangents)` -- one port, the cotangent. A member that needs a value the body
    // reads and no type of the program can supply declares one port *more*, and the call site then
    // hands the graph one argument where it has two ports: the run dies on
    // `nargs.size() == normPorts.size()`, which is a signal and says nothing. Rather than let the
    // shape reach the run, the build compares the two counts here -- the only place that has both --
    // and refuses by name. The count is not the rule: it is the one reading that can tell "this
    // member is the function the call site was promised" from "this member is a different function".
    //
    // Only the operator's own call site is compared: `promisedToTheOperator` is what the kernel says,
    // and it is false for a member the walk builds for a *call inside a body*, whose caller writes its
    // type with the captures in hand (`calleeNeeds`) and therefore fills them. Comparing there would
    // refuse the honest `index_into` over an array whose extent only the caller knows -- the value is
    // carried, the adjoint is built from `ct` and a known value, and the rule allows exactly that.
    if (promisedToTheOperator) {
        FunctionType *const promised =
            function->funcType() != nullptr ? transposeSignature(function->funcType()) : nullptr;
        const size_t promisedPorts = promised != nullptr ? promised->normTypesCount() : 0;
        const size_t builtPorts    = made != nullptr ? made->normPorts().size() : 0;
        if (promisedPorts != 0 && builtPorts != promisedPorts) {
            // Which repair the reader is owed depends on *what* the extra port carries: a value a
            // law reads that no type states (a capture the operator's type cannot name) or the shape
            // of a parameter whose declaration leaves it open. The second has a declaration to edit
            // and a rule behind it (author, 2026-11-03 16:15), so it is refused with the diagnostic
            // that says so; the first keeps the sentence about the closure.
            TransposeRefusal refusal;
            refusal.graph = function->name();
            if (state.shapeWanted) {
                refusal.cause  = TransposeRefusalCause::ParameterShape;
                refusal.param  = state.shapeParam;
                refusal.paramName = state.shapeName;
                refusal.paramType = state.shapeType;
            } else {
                refusal.cause = TransposeRefusalCause::NeedsAValueOutsideTheClosure;
            }
            // The line names the cause by the table's word rather than the log's `skip=` prefix: this
            // record is written after the walk, and `skip=` belongs to the one place that renders a
            // refusal as the walk meets it. What a reader greps for is here all the same.
            const char *const word = transposeRefusalWord(refusal.cause);
            os << "TRANSPOSE function=" << function->name() << " ports=" << builtPorts
               << " promised=" << promisedPorts << " cause=" << (word != nullptr ? word : "-");
            if (state.shapeWanted) {
                os << " param=" << state.shapeParam << " param-type=" << state.shapeType;
            }
            os << '\n';
            return TransposeGraph{nullptr, nullptr, refusal};
        }
    }
    return TransposeGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}



} // namespace camel::transforms
