// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a transposition asks of a law before it builds anything, and the shape a law's answer is
 * compared against -- `transpose/rules.h` declares it. The choice itself is
 * `rule_expand.cpp#chooseRuleAmong`; what this file owns is the type tuple the ports are read from
 * and the words a refusal of this family is written with.
 */

#include "transpose/rules.h"

#include "camel/core/forward.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/text.h"
#include "camel/runtime/graph.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::transforms {

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;

/// The type a law's right side takes its cotangent at: the port of the closure it answers with
/// (`rule_expand.h#ruleCotangentPortsOf`), or null when the law has no instance at `instanceTypes`
/// or its right side is not a function of the tangents.
///
/// `rulePortsOf` is the **left** side's ports -- the operator's own operands -- and a cotangent is
/// not one of them. A law is therefore chosen on the shape of the operands it describes, and ready
/// to read whatever is handed to it: what a law says about the cotangent is written in the closure
/// its right side answers with (`(ct: T[..]) => mean_axis_cotangent(ct, at)`), and that is the only
/// place the port is stated. This is the question `ruleCotangent` asks before it hands the value
/// over.
Type *cotangentPortOf(
    camel::core::context::Context &context, const std::string &rule,
    const std::vector<Type *> &instanceTypes, size_t position) {
    std::string why;
    const camel::compile::gir::GenericRuleInstance *instance =
        camel::compile::gir::genericRuleInstanceByName(context, rule, instanceTypes, {}, why);
    if (instance == nullptr) {
        return nullptr;
    }
    const std::vector<Type *> ports = camel::transforms::ruleCotangentPortsOf(*instance);
    if (ports.empty()) {
        return nullptr;
    }
    return position < ports.size() ? ports[position] : ports.front();
}

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
/// (`rule_expand.cpp`; its `{Float64()}` is a standing defect that is not this function's) -- and the words a refusal
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
    const std::vector<bool> *answered, const std::vector<Type *> *offeredOperands, bool *lawFits) {
    std::vector<Type *> actual;
    actual.reserve(inputs.size() + 1);
    if (offeredOperands != nullptr) {
        // What the walk offers *this* ask: a law of the reduction family names one port more than
        // its operator has, and that port is the extents (`sum_from_type_t`). The list is the
        // operands' own types with that one appended, so a law that does not name it is unaffected.
        actual = *offeredOperands;
    } else {
        for (const gc_node_ref_t input : inputs) {
            actual.push_back(const_cast<Type *>(typeOf(input)));
        }
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
                if (Type *elementType = array->elemType()) {
                    element = elementType;
                }
            }
        }
        instanceTypes.push_back(element);
    }
    instanceTypes.insert(instanceTypes.end(), actual.begin(), actual.end());
    RuleChoiceWords words{"law", "operands"};
    // What is *not* a mismatch here is a rank: 5.4 says `T[..]` includes rank 0, a rank-zero array
    // being the element itself, so a port written `T[..]` takes a `float` and one law covers both
    // shapes `tensor:multiply` is called with. There is no clause refusing a non-array operand for an array port, and
    // the array-and-scalar product has a law of its own (`mul_tensor_t`).
    //
    // The instance the ports are read from is made at the operands' own types and not at one
    // `float`: a law is judged at the types the expansion will build it at, and a law that takes
    // one type parameter per shape (`matmul_t` serving the matrix and both vector lanes) has no
    // instance at all when it is asked for one `float` (`it takes 3 type parameter(s), and it was
    // asked for 1`). That sentence was this choice's reading on the merged tree -- the three lanes
    // unreachable behind it -- and it is what a standing `{Float64()}` costs once laws are written
    // per shape.
    RuleChoice choice = chooseRuleAmong(
        context,
        camel::core::transposeRulesOf(std::string(uri)),
        instanceTypes,
        actual,
        dataOperandCount,
        words,
        answered);
    if (!choice.found() && choice.refusal != RuleRefusal::Shape) {
        // The law could not be built at one type per port. That is not this question's to refuse: a
        // law whose right side the expansion can still write is not a law that says this step is
        // not its shape, and refusing here reports the next step's gap as this one's. Ask again at
        // the
        // **one** type the law is written for -- a law that names a single type parameter for
        // several positions (`add_t::<T>(x: T, c: T)`) is instantiated at one type, and the ports
        // it then has are that type's ports (`it takes 1 type parameter(s), and it was asked for 3`
        // is what one-per-port answers it).
        //
        // That one type is the step's own: were it `Float64()`, a narrow
        // float step would fall back to a `float` instance, whose ports take no `f32` operand, so
        // the choice would refuse by shape and the walk would stop -- `vjp` of `(x: f32) => x
        // + 1.5` would read `skip=rule-shape uri=:op/add_f reason=the rule takes (float, float), and the
        // operator has (f32, f32)`. The cotangent's element type is the step's own: an `f32`
        // cotangent's element is `f32`, a `float` cotangent's is `float`, and the operands of a
        // step are the types that cotangent is a cotangent of.
        Type *soleType = instanceTypes.empty() ? nullptr : instanceTypes.front();
        // No element type means there is no instance to ask about: the tuple stays empty -- the
        // same "nothing to try" the choice answers around, which the walk turns into `rule-shape`
        // with the operands it really has. A `float` here is an instance asked for a step whose
        // cotangent is not a `float`, which is the shape this whole change removes.
        std::vector<Type *> soleTypes;
        if (soleType != nullptr) {
            soleTypes.push_back(soleType);
        }
        // GUARD for the empty tuple, so that it is not read as a question nobody asked: this line
        // is reached only after the one-per-port reading above answered something other than
        // `Shape`, and `soleType` is `instanceTypes.front()` -- the types `lawInstanceTypes` built
        // from the step's own operands. Its empty case is an operator with no operand, and a null
        // front is a null operand type, which that function lets through on purpose so the
        // expansion says what it could not build in its own words. An empty tuple handed to
        // `chooseRuleAmong` is answered as `Shape` (no instance found), which is the sentence the
        // walk then refuses with; it is never handed to the expansion as a tuple of one invented
        // type.
        choice = chooseRuleAmong(
            context,
            camel::core::transposeRulesOf(std::string(uri)),
            soleTypes,
            actual,
            dataOperandCount,
            words,
            answered);
        // `operandTypes` stay the node's own in both readings: the shape test is about the operands
        // the walk will hand the right side, whatever instance the ports were read from.
        if (!choice.found()) {
            choice.operandTypes = actual;
        }
    }
    if (!choice.found() && choice.refusal != RuleRefusal::Shape) {
        // The key files a law and no instance of it can be made at any tuple tried: a law written
        // per rank, meeting a rank outside the set it serves (`matmul_t` at `float[3, 2, 2]`). That
        // is not "no law for this step" -- the index has one -- so the walk is handed the law's
        // name and the expansion says what it could not build (`rule-ranks`). An empty key is the
        // case this class exists for, and it stays empty here.
        //
        // The name is synthesized, and it is told apart from a fit the index really made: a caller
        // that wants to ask a second question (the extents a law of the reduction family names)
        // must not read a name handed back for its *message* as an answer.
        const std::vector<camel::core::RuleMatch> filed =
            camel::core::transposeRulesOf(std::string(uri));
        if (!filed.empty()) {
            choice.rule = filed.front().rule;
            choice.why.clear();
        }
    } else if (lawFits != nullptr) {
        *lawFits = choice.found();
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
std::string operandRanksText(
    const camel::graph::GraphView *function, std::span<const gc_node_ref_t> operands) {
    std::string text;
    for (const gc_node_ref_t operand : operands) {
        Type *type  = function != nullptr ? const_cast<Type *>(function->typeOf(operand)) : nullptr;
        size_t rank = 0;
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

/// Whether a law's answer and the operand it answers for state shapes that cannot both be right,
/// and -- when they cannot -- the sentence that names both spellings.
///
/// This is the check made at the transposition itself, and the class
/// it covers is not the broadcast: it is **a law written for the wrong shape**. A rule's right side
/// answers one component per port, in the order its left side writes them, and the index files a law
/// under its operator's head and that head's operand count and reads no operand types -- so nothing
/// ties a component's shape to the shape of the operand whose cotangent it claims to be.
/// `add_tensor_t` answered `ct` for a `float[3]` operand of a `float[2, 3]` sum: a `[2, 3]` tensor
/// under a static type that said `[3]`, exit 0 and no diagnostic.
/// `subtract_t` answered the same shape negated, and `where_t` answers both branches at the
/// condition's shape.
///
/// What can be judged is what the two types **state**, and nothing more:
///
///   * an extent a type leaves open (`float[]`, `float[n, 3]` over a name nothing binds) states no
///     extent at that axis and is not compared there;
///   * a side whose rank is open (`T[..]`, `T[3, ..]`) states no rank at all -- `T[..]` includes
///     rank 0, a rank-zero array being the element itself (design principles 5.4) -- so such a side
///     is not compared on rank either;
///   * a value that is not an array is a rank-zero value (5.4), so a number answered for an array
///     operand is a rank disagreement and is named as one.
///
/// Element types are not compared here. The law's *cotangent port* against the value handed to it is
/// a different question with its own cause (`CotangentPortMismatch`), and a differing width at this
/// site would be a claim about the arithmetic rather than about the shape.
///
/// The read is deliberately narrow: the two hedges above are what keep a check on a law from
/// refusing an answer it cannot fault. Every law that reduces a broadcast -- `sum_to(...)`, the
/// repair this check was added beside -- answers `T[..]` and says nothing here; the answers it can
/// fault are the ones a law gave a shape its operand cannot have.
std::optional<std::string> statedShapeDisagreement(const Type *answer, const Type *operand) {
    if (answer == nullptr || operand == nullptr) {
        return std::nullopt;
    }
    // A `Ref` is a type that is not decided yet, so nothing about its shape is a fact -- the same
    // exception the port comparison below makes, for the same reason.
    if (answer->code() == camel::core::type::TypeCode::Ref ||
        operand->code() == camel::core::type::TypeCode::Ref) {
        return std::nullopt;
    }
    const auto arrayOf = [](const Type *type) -> const ArrayType * {
        return type != nullptr && type->code() == camel::core::type::TypeCode::Array
                   ? tt::as_ptr<ArrayType>(const_cast<Type *>(type))
                   : nullptr;
    };
    const ArrayType *const answerArray  = arrayOf(answer);
    const ArrayType *const operandArray = arrayOf(operand);
    // The rank each side states: a number states rank 0 and is never open, an array states the axes
    // of its prefix and is open when its type says the rank is the value's.
    const size_t answerRank  = answerArray != nullptr ? answerArray->rank() : 0;
    const size_t operandRank = operandArray != nullptr ? operandArray->rank() : 0;
    const bool answerOpen    = answerArray != nullptr && answerArray->anyRank();
    const bool operandOpen   = operandArray != nullptr && operandArray->anyRank();
    const auto both = [&]() {
        return "the law answers a `" + camel::core::type::typeToString(answer) +
               "` for an operand of type `" + camel::core::type::typeToString(operand) + "`";
    };
    if (!answerOpen && !operandOpen && answerRank != operandRank) {
        return both();
    }
    if (answerArray == nullptr || operandArray == nullptr || answerOpen || operandOpen) {
        // One side is a number or states no rank, and the extents of a rank that is not stated are
        // not a fact this check has: nothing left to compare.
        return std::nullopt;
    }
    // Both ranks are stated and equal, so the axes line up one for one; an axis one side leaves
    // open is skipped, and an axis both state must state the same number.
    const auto answerExtents  = answerArray->extents();
    const auto operandExtents = operandArray->extents();
    for (size_t axis = 0; axis < answerExtents.size() && axis < operandExtents.size(); ++axis) {
        const std::optional<int64_t> answered = answerExtents[axis].evaluate({});
        const std::optional<int64_t> expected = operandExtents[axis].evaluate({});
        if (!answered.has_value() || !expected.has_value()) {
            continue;
        }
        if (*answered != *expected) {
            return both();
        }
    }
    return std::nullopt;
}

/// Whether the rule named `ruleName` describes a node whose ports are `operandTypes`.
///
/// It does not, and this is why the question is asked here rather than left to the index: a rule is
/// filed under the head of its left side and that head's operand count, and the index does not read
/// the operand *types* (`rule_head.cpp`). So a law can be found for a node it was not written for
/// -- a law whose parameters are two fixed-length arrays meeting an open-rank value is the shape
/// that has to be refused by name rather than expanded into something nobody wrote.
///
/// The choice of *which* law answers is `chooseRuleAmong`'s (`rule_expand.cpp`), and it asks this
/// same question one step earlier, at the instance `expandRuleByName` will build the right side
/// from. This is that question asked again at the types the expansion will really use -- the
/// cotangent's own type and the operands' -- because a law that covers two shapes of one operator
/// is judged at the types the node has, not at one `float` for every port. One criterion, two
/// instantiations: not two classifications.
///
/// What is *not* a mismatch is a rank. Design principles 5.4 says `T[..]` includes rank 0, that a
/// rank-zero array is the element itself, the same value and the same type. So a port written
/// `T[..]` takes a `float`, one law covers both shapes `tensor:multiply` is called with (two
/// arrays, and an array and a number). A rank a law does not serve is a body that
/// will not instantiate, which is a different fact and has its own word (`RuleRanks`,
/// `rule-ranks`).
///
/// The cotangent each position answers is then `Tangent<P>` of that position's own type, which is
/// what makes a number's cotangent a number: `sum_to(ct * x, w)` answers `w`'s own type (`S`, the
/// type of `like`), so the law has to be instantiated at the operand types the node really has
/// (`rule_expand.cpp#expandRuleByName`), not at one `float` for every port.
/// The type tuple a law has an instance at, from the types its ports are handed.
///
/// Usually the ports' own tuple. A law that names one type parameter for several positions
/// (`add_t::<T>(x: T, c: T)`, every scalar law of the family) has no instance at one type per port
/// -- asking answers `it takes 1 type parameter(s), and it was asked for 3` -- and is instantiated
/// at
/// **one** type instead: the step's own *element* type, which is the rank-zero type of the first
/// entry (5.4: the element of a rank-zero array type is the type itself, so this is the operand's
/// own type for a number and `float` for a `float` array).
///
/// Were it `Float64()`, an `f32` step would have no instance
/// whose ports take an `f32` operand and the walk would refuse by shape. Neither is it the first entry
/// as it stands: handing an array in as `T` gives a law that writes `T[..]` a port of `T[..][..]`,
/// and array gradients would run out of memory. The tuple is decided here once, so
/// the shape test, the choice and the expansion cannot disagree about which instance is under
/// discussion.
std::vector<Type *> lawInstanceTypes(
    camel::core::context::Context &context, const std::string &ruleName,
    const std::vector<Type *> &portTypes) {
    std::string unused;
    std::vector<Type *> types = portTypes;
    // A law instantiated at no operand type at all: there is nothing to ask and nothing to reduce,
    // so the tuple stays empty and the caller reads that as "no instance to try". A `float` pushed
    // in here would be an instance asked about a type no step has.
    //
    // GUARD for the empty case, so that it is not read as a reading nobody took: `portTypes` is the
    // node's own operand-type list (`operandTypes` at every call site), so empty means an operator
    // node with **no operand at all** -- not a type that could not be read. Every rule in
    // `stdlib/rulesets/transpose.cml` is written for at least one operand and names at least one
    // type parameter (`export rule mul_t::<T>(x: T, c: T)` and its siblings), so such a node has no
    // instance to be given; pushing `Float64()` here would be the one instance it can never have.
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
            if (Type *elementType = array->elemType()) {
                sole = elementType;
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
    const std::vector<Type *> &operandTypes, const std::vector<bool> &answered, std::string &why) {
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
    // A rule names the node's ports, in port order: the values, then the `<>` ones
    // (`reshape<shape>` names one of the two). Naming more ports than the node has cannot be
    // filled; naming all of the node's values is what a law for this operator does.
    const bool namesTheNode = count > 0 && count <= operandTypes.size();
    bool takes              = namesTheNode;
    for (size_t i = 0; takes && i < count; ++i) {
        // A position the walk answers no cotangent for -- a constant, a value with no tangent space
        // -- is not a position the law is asked about (`isConstant`, `hasTangentSpace` in the
        // walk), so its shape cannot make the law wrong: `x * 2.0` is one array operand and one
        // constant, and the law for two arrays answers it correctly. What is checked is the
        // operands the walk will hand the right side.
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

} // namespace camel::transforms
