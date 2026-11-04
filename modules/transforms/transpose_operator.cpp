// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose_operator.h"

#include "readings.h"
#include "transpose.h"
#include <sstream>

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <iostream>
#include <optional>
#include <string>

namespace camel::transforms {

namespace {
namespace mm = camel::core::mm;
using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::toSlot;
using camel::core::type::FuncTypeResolver;
using camel::core::type::FunctionType;
using camel::core::type::FunctionTypeFactory;
using camel::core::type::type_vec_t;
using camel::core::type::TypeCode;

/// The type of `transpose(f)`: `(f: (float) => float) => (cotangent: float) => (float,)`. The call
/// takes the cotangent of `f`'s result and answers one cotangent per parameter of `f` -- every
/// parameter is a linear input (author, 2026-11-02 11:30), so the transposed function takes nothing
/// else and a value the body needs is captured by the function that is transposed. The parameter is
/// named, so the type prints the way the declaration writes it
/// (`stdlib/signatures/transforms.cml`), and the two are one string.
FunctionType *transposeOperatorSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    // What `f` may be is `transposeSignature`'s to say: it is the shape law, and it is asked by the
    // compiler before the call is built. This only names the parameter the declaration writes.
    FunctionType *transposed = transposeSignature(function);
    if (transposed == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(transposed);
    return factory.build();
}

/// transpose: `(f: (float) => float) => (cotangent: float) => (float,)`. The cotangent of `f`'s
/// result goes in and the cotangents of `f`'s parameters come out, one member per parameter -- the
/// pullback of a linear function. Every parameter of `f` is a linear input, so the transposed
/// function is passed no value: `2*x` and `3*x` in the same program are two rules over the same
/// parameter, which is why a value used k times arrives as k cotangents that add.
class TransposeResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm) const override {
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        FunctionType *transposed = transposeOperatorSignature(tt::as_ptr<FunctionType>(norm[0]));
        if (transposed == nullptr) {
            return std::nullopt;
        }
        return transposed;
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm,
        type::static_args_t normStatics) const override {
        (void)normStatics;
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        const auto *function = tt::as_ptr<FunctionType>(norm[0]);
        // Whether `f` has a transpose is `transposeSignature`'s to say, and it is the same answer
        // the call site's type was built from. What is left here is *why* it said no, and the
        // causes are asked in the order the law asks them -- a second spelling of the shape would
        // be a second law, and it drifts: the one that stood here still said "one `float`
        // parameter, or the backward half" while several `float` parameters had been accepted.
        if (transposeSignature(const_cast<FunctionType *>(function)) != nullptr) {
            return std::nullopt;
        }
        // The exit cause, asked of the type: the cotangent of the result *is* the result's tangent
        // space, so a `float` result takes a `float` cotangent and an `f32` result takes an `f32` one
        // (`Tangent<f32>` is `f32`; `stdlib/types.cml#Tangent`, `camel/core/tangent_space.h`). What
        // the law then needs is the addition of that space -- `:op/add_d` at `float`, `:op/add_f` at
        // `f32` (`transpose.cpp#addUriFor`) -- so those are the two widths a result can have here.
        // This is the *reason* the law above said no and not the law: `transposeSignature` is what
        // decides, and it is asked first, one branch up.
        const camel::core::TangentSpaceAnswer space =
            camel::core::tangentSpaceOf(function->exitType());
        const bool hasCotangentWidth =
            space.hasSpace() && space.space != nullptr &&
            (space.space->code() == TypeCode::Float64 || space.space->code() == TypeCode::Float32);
        if (!hasCotangentWidth) {
            return std::string(
                "`transpose` takes a function whose result has a cotangent: the cotangent of the "
                "result is what goes in, it is the result's own tangent space (`Tangent<f32>` is "
                "`f32`), and this law adds cotangents with the addition of that space -- `:op/add_d` "
                "at `float` and `:op/add_f` at `f32`");
        }
        return std::string(
            "`transpose` takes a function whose parameters are all linear: each one is what a "
            "cotangent is answered for, so a parameter whose type holds no cotangent -- an integer, "
            "a `bool`, a string -- leaves the function with no transpose. A value the body needs is "
            "captured by the function that is transposed, not passed to the transposed one");
    }

    std::string signature() const override {
        return "(f: (float) => float | (float, ...) => float) => "
               "(cotangent) => (cotangents..)";
    }

    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// The sentence a reader is handed, rendered from the refusal the build recorded.
///
/// The cause is a value here, not a word read back out of the log: `buildTransposeMember` recorded it
/// where the walk stopped, the log spells that same value for a reader who greps, and this spells it
/// for one who is told what went wrong. A cause with no sentence of its own falls to the sentence
/// that names the shape -- the catch-all prints the log's own word for it, so a cause added without a
/// sentence says which cause it was instead of saying nothing.
///
/// Only the cause is decided here -- the prefix `transpose has no transpose for this function` stays
/// where the raise site writes it, because it is what a reader greps for and what the plans pin. The
/// *where* the walk recorded (`TransposeRefusal`'s graph, node, uri and lookup) is rendered with the
/// cause it belongs to, and not here as a second decision: a site records it once, and the log and
/// the sentence read that one record.
std::string refusalSentence(const TransposeRefusal &refusal) {
    const std::string position =
        refusal.param.has_value()
            ? "the parameter at position " + std::to_string(*refusal.param + 1)
            : std::string{};
    const auto namedShape = [&refusal]() {
        const char *word = transposeRefusalWord(refusal.cause);
        return std::string("the shape this program used has no transpose (the transform named it `") +
               (word != nullptr ? word : "-") + "`)";
    };
    switch (refusal.cause) {
    case TransposeRefusalCause::None:
        // A build that refused before any site ran records no cause. Saying so is better than a
        // sentence about a shape nothing named.
        return "the transform gave no reason";
    case TransposeRefusalCause::CotangentNeedsAValue:
        return position.empty() ? "a parameter holds no cotangent: nothing uses its value"
                                : position + " holds no cotangent: nothing uses its value";
    case TransposeRefusalCause::UnreadParameter:
        return position.empty() ? "a parameter is never read by the body, so it answers no cotangent"
                                : position + " is never read by the body, so it answers no cotangent";
    case TransposeRefusalCause::NeedsAValueOutsideTheClosure:
        return "the member reads a value that the transposed function does not carry in its closure, "
               "so there is nothing to capture for it";
    case TransposeRefusalCause::NoCotangentArithmetic:
        // Named as what it is: the value is read more than once and its *type* has no sum, so there
        // is no cotangent to answer -- not a width the transform failed to support, and not a program
        // shape a reader can write differently without changing what it differentiates.
        return "a value the body reads twice has a cotangent type with no addition of its own, so its "
               "cotangents cannot be summed";
    case TransposeRefusalCause::ParameterShape:
        // The words are the registry's and not this file's: the kernel below raises this same cause
        // as `RuntimeDiag::TransposeNeedsParameterShape`, so a reader is handed the diagnostic the
        // language declares for the rule, and this case reads that message instead of writing a
        // second one that could drift from it. The parameter is named where the signature has names
        // and placed where it does not.
        return camel::core::error::runtimeDiagMessage(
            RuntimeDiag::TransposeNeedsParameterShape,
            {refusal.graph,
             refusal.paramName.empty() ? position : "the parameter '" + refusal.paramName + "'",
             refusal.paramType});
    case TransposeRefusalCause::State:
        return "a loop carries its state out of itself, and that shape has no transpose";
    case TransposeRefusalCause::CotangentPortMismatch:
        // **Measured unreachable from any program, so this is not a language rule** (author,
        // 2026-11-04, second ruling on this item: an internal inconsistency that no program reaches
        // must say so, and must not be written as "the language does not allow this"). The two
        // spellings are handed over verbatim because the repair is an edit to one of them -- the
        // law's port and the value the step really has -- but the sentence names the fault as the
        // compiler's: a law filed under its operator's head and operand count cannot be tied to the
        // shape a step has, and a program cannot make the two disagree (its cotangent is
        // `Tangent<ResultOf<F>>`, the type the declaration states). The check stays because it
        // guards a real internal inconsistency; it is a guard, not a judgement about programs.
        return "internal error: the rule for '" + refusal.uri + "' takes its cotangent as `" +
               refusal.portType + "` and this step has a `" + refusal.handedType +
               "`; a law's port disagreeing with the value the walk hands it is a compiler defect and "
               "not the program's fault";
    case TransposeRefusalCause::CalleeBuiltFromAParameter:
        // **The language's rule, and the parameter it is about** (author, 2026-11-04). `transpose`
        // answers the map a linear function is; a call whose callee the body *built* out of a
        // parameter applies a function that parameter produced, so the function being transposed is
        // not linear in that parameter. The sentence names the parameter because that is the edit a
        // reader makes, and it says "not linear" and not "the answer has no source": the second
        // describes this walk, the first describes the language.
        return "this function is not linear in " +
               (refusal.paramName.empty() ? position : "the parameter '" + refusal.paramName + "'") +
               ": it builds the function this call applies out of that parameter";
    case TransposeRefusalCause::CallNoTranspose:
        // One of the two causes a call site can meet, and the one this file said for both of them
        // until 2026-11-03. It is true only here: the callee has no transpose to call. A callee
        // whose transpose *was* built, and whose member reads a value the call site cannot hand
        // over, was refused with this same sentence, and the reading above the refusal contradicted
        // it -- `TRANSPOSE function=rulesets.jvp::index_into<float> graph=307 type=(int, float[],
        // float) => (float[],) ... rhs=idx_arr_t` says the member exists. That shape is the word
        // below, whose position is the callee's own parameter index.
        return "this call's callee has no transpose of its own, so the call cannot be turned around";
    case TransposeRefusalCause::CallValueMissing:
        if (position.empty()) {
            // Naming only what the reading says: the member exists, and what it wanted has no
            // source at the call. Nothing here answers the value with one it does not have.
            return "the callee's transpose was built, but it reads a value this call does not "
                   "carry";
        }
        return "the callee's transpose was built, but " + position +
               " reads a value this call does not carry";
    case TransposeRefusalCause::CallValueComputedByTheBody:
        // The other shape, and the one the sentence above used to cover: the value is not missing
        // from the call -- it is a value the function's own body computes, and it comes out of a
        // loop or a branch. Handing it over is not passing one more argument; it is turning the
        // region that computes it around, which is a transposition of a whole body.
        return position.empty()
                   ? std::string("the callee's transpose was built, but it reads a value the "
                                 "function's own body computes out of a loop or a branch, so "
                                 "carrying it means transposing that body")
                   : "the callee's transpose was built, but " + position +
                         " reads a value the function's own body computes out of a loop or a "
                         "branch, so carrying it means transposing that body";
    case TransposeRefusalCause::Rule:
        // The one cause whose sentence is built from the *where* as well as from the cause: which
        // function, which node, which operator, and which of the three ways the index had no rule.
        // The walk recorded all four at the site, and the log has printed all four since it existed
        // -- the sentence said "a step of this body" and left a reader with no step to look at.
        // A part the walk recorded as nothing is said to be missing rather than dropped: a gap
        // reads as "there is no such thing", which is the reading this sentence exists to prevent.
        {
            // Each of the three slots below can say "the walk recorded nothing here", and none of
            // those three sentences is reachable today: every graph the walk can enter has a name --
            // a written-out lambda gets a synthesised one (`__g_48__` in the probe that measured
            // it, 2026-11-03) -- and the one site that records a `Rule` refusal always knows the
            // node and its operator. They are written out rather than left as a gap because a gap
            // reads as "there is no such thing"; and if one of them ever fires, the reading is that
            // naming or the site's record is where the defect is, not the program -- so the shape
            // that reaches one is a compiler defect to fix, never a sentence to widen.
            std::string about = "a step";
            about += refusal.graph.empty() ? " of a graph the walk did not name"
                                           : " of `" + refusal.graph + "`";
            std::string at;
            at += refusal.node.has_value() ? "node " + std::to_string(*refusal.node)
                                           : "a node the walk did not number";
            at += refusal.uri.empty() ? ", an operator the walk did not name"
                                      : ", operator `" + refusal.uri + "`";
            // The three ways the lookup answers nothing are three different facts about the tree,
            // and a reader fixes a different thing for each: a module that was not built, a family
            // that cannot spell the operator's name, or a family that is here and holds no rule.
            std::string found;
            if (refusal.lookup == "no-such-family") {
                found = "no module named `rulesets.transpose` was built into this program, so the "
                        "family holds no rule for any operator";
            } else if (refusal.lookup == "no-rule") {
                found = "the family is built, and its index holds no rule under that operator's "
                        "name at the operand count it has";
            } else if (refusal.lookup == "unbound") {
                // The key itself was missing, and the site's word does not say which way: a family
                // that was never built spells no head at all, and an operator whose name the index
                // does not spell has none either. So the sentence names the key's origin and both
                // ways it can be absent, instead of claiming the family is here (measured: the
                // missing-module shape reaches this branch when the step is a module operator,
                // whose key comes from the family's index -- the shape `no-such-family` covers only
                // an operator the family could spell without the module, like `:op/mul_d`).
                // The three facts the walk can prove, and nothing more. "The family holds no rule
                // for this operator" is the index's own reading (`candidates.empty()`); "transpose
                // is for linear functions" is the language rule; and `vjp` is the way to take a
                // gradient. What is *not* said is that the operator is not linear in its k-th
                // parameter: that would need the forward family's index, and a program that only
                // imports `rulesets.transpose` does not carry it -- so the claim is out of reach
                // here, and a wrong explanation is worse than none, because a reader acts on it
                // (docs/handover/questions/2026-11-04-must-a-transpose-refusal-claim-the-operator-is-not-linear.cn.md).
                found = refusal.uri.empty()
                            ? "the family holds no rule for that operator; `transpose` is for linear "
                              "functions, so take the gradient with `vjp`"
                            : "the family holds no rule for `" + refusal.uri +
                                  "`; `transpose` is for linear functions, so take the gradient "
                                  "with `vjp`";
            } else {
                found = "the walk recorded no reason for the lookup";
            }
            return about + " (" + at + ") is one the transpose family `rulesets.transpose` cannot "
                   "turn around: " + found;
        }
case TransposeRefusalCause::RuleShape: {
        // The other half of the same lookup, and the sentence `Rule` above used to stand in for.
        // The index files a rule under the head of its left side and that head's operand count and
        // reads no operand types, so a key can hold a law written for another shape: this step has
        // a law, and the law's own parameter list says this step is not it. "No rule" would be a
        // sentence about a state this transform is not in. Everything a reader can act on is the
        // operator, the two type lists and the node, so all three are in the sentence; the operator
        // is the record's `uri`, the same field `Rule`'s sentence names, because it is the same
        // fact -- the key the law is filed under.
        std::string answer = "the rule the index filed for ";
        answer += refusal.uri.empty() ? std::string("this step") : "`" + refusal.uri + "`";
        answer += " was written for another shape";
        if (!refusal.shape.empty()) {
            answer += ": " + refusal.shape;
        }
        if (refusal.node.has_value()) {
            answer += " (node " + std::to_string(*refusal.node) + ")";
        }
        return answer;
    }
    case TransposeRefusalCause::RuleRanks: {
        // The neighbour of the sentence above, and the one that used to be answered with the
        // builder's own words ("No matching function found for argument types"), which name a helper
        // rather than the law and never say the rank. Here the law's pattern *took* the operands: the
        // law was chosen, and its body -- the cotangent helpers a law that serves several shapes
        // writes, one overload per shape -- has nothing for the ranks this step has. So the sentence
        // names the law, the operator it is filed under, and the ranks, because those three are
        // what a reader edits or writes a case for.
        std::string answer = "the law ";
        answer += refusal.rule.empty() ? std::string("the index filed") : "`" + refusal.rule + "`";
        answer += " has no cotangent for ";
        answer += refusal.uri.empty() ? std::string("this step") : "`" + refusal.uri + "`";
        answer += " at these operands";
        if (!refusal.ranks.empty()) {
            answer += ": their ranks are " + refusal.ranks;
        }
        answer += ", and it serves the ranks its cotangents are written for";
        if (refusal.node.has_value()) {
            answer += " (node " + std::to_string(*refusal.node) + ")";
        }
        return answer;
    }
    case TransposeRefusalCause::Condition:
        return "a bool condition has no cotangent space, so it carries no derivative";
    case TransposeRefusalCause::Signature:
    case TransposeRefusalCause::Exit:
        return "the graph's shape disagrees with the signature the call was written against";
    // No program in this tree reaches the shapes below yet, so each keeps the sentence that names
    // the shape instead of a sentence written for a cause nobody has read; when one becomes
    // reachable, give it the sentence it stands for.
    case TransposeRefusalCause::Arm:
    case TransposeRefusalCause::ArmTangents:
    case TransposeRefusalCause::Component:
    case TransposeRefusalCause::Join:
    case TransposeRefusalCause::Kind:
    case TransposeRefusalCause::Output:
    case TransposeRefusalCause::Range:
        return namedShape();
    }
    return namedShape();
}

/// transpose(f) at run time: the new graph, as a function value. Nothing of `f` is changed, and the
/// value keeps `f`'s closure.
slot_t transposeKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: transpose needs a function value");
    }
    // The reading log is what a reader greps and what the plan's cases assert, so it is captured and
    // then forwarded: a tee, not a replacement. *Why* the build refused is not read back out of it --
    // `built.refusal` is what the walk recorded, and the sentence below is its reading.
    std::ostringstream why;
    const TransposeGraph built = buildTransposeGraph(
        ctx.shared_from_this(), function->program(), function->graph(), why,
        /*promisedToTheOperator=*/true);
    readings() << why.str();
    if (built.graph == nullptr) {
        // A cause the language has a rule for is raised as the diagnostic the registry declares for
        // it, not as the catch-all: `ParameterShape` is the shape rule -- the adjoint of a linear map
        // needs the shape of its domain, and the program's type does not state it -- so a reader is
        // told to write the shape in the type (author, 2026-11-03 16:15). Everything else keeps the
        // sentence `refusalSentence` renders from the cause.
        if (built.refusal.cause == TransposeRefusalCause::ParameterShape) {
            const std::string named =
                built.refusal.paramName.empty()
                    ? (built.refusal.param.has_value()
                           ? "the parameter at position " +
                                 std::to_string(*built.refusal.param + 1)
                           : std::string("a parameter"))
                    : "the parameter '" + built.refusal.paramName + "'";
            throwRuntimeFault(
                RuntimeDiag::TransposeNeedsParameterShape, built.refusal.graph, named,
                built.refusal.paramType);
        }
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "transforms: transpose has no transpose for this function: " +
                refusalSentence(built.refusal));
    }
    const std::string spelled = TransposeResolver{}.signature();
    const auto resolved = TransposeResolver{}.resolve({}, type_vec_t{function->graph()->funcType()});
    const std::string produced = resolved ? (*resolved)->toString() : std::string("-");
    const std::string given =
        (resolved && (*resolved)->hasExitType()) ? (*resolved)->exitType()->toString()
                                                 : std::string("-");
    const std::string handedBack =
        built.graph->funcType() != nullptr ? built.graph->funcType()->toString() : "-";
    readings() << "TRANSPOSE op signature=" << spelled << " resolved=" << produced
              << " transposed=" << given << " graph=" << handedBack
              << " same=" << (given == handedBack ? "yes" : "no") << '\n';
    // The ports the member takes against the parameters the operator promised. A call site fills the
    // parameters a type writes and nothing else, so a member that takes one more is refused here
    // instead of being met by the run: both facts are in hand -- the resolver answered the promise
    // just above, and the graph is the one just built -- and the reading one line up already spells
    // the difference as `same=no`.
    //
    // What the extra parameter carries is a parameter's **value**, and `transpose`'s law says a
    // parameter of the function is a linear input whose cotangent is answered for and whose value is
    // never passed (author, 2026-11-02 11:30). The body asked for the value because a cotangent of
    // that parameter is built from it: `idx_arr_t` answers a zero array of the operand's shape, and
    // `mean_axis_cotangent` reads `xs` for `numel`/`ones_like`. That ask is a fact of the body and
    // not of the type, so the two sides disagree on how many parameters the transposed function
    // has, and the run could only say so as a count (`nargs` against `normPorts`), naming neither
    // the parameter nor the reason: measured 2026-11-04 on `pick(x: float[3]) = x[1]`, the fast VM
    // aborts (`expected 2, got 1`) and `std::nvm` reports a defect of the compiler instead of a fact
    // about the program.
    //
    // This refusal is also what leaves `RuntimeDiag::PassInternalError`'s norm-arity text unreached
    // from `transpose`: no call of a member is built any more whose argument count the member's own
    // ports do not fit, because the operator stops where the two counts are known. The pass-level
    // report is kept as the internal fallback it is for every other pass -- its help is written for
    // the whole pipeline and is true there, so it is not rewritten for one operator's old wall.
    {
        FunctionType *const promised     = resolved ? *resolved : nullptr;
        FunctionType *const member       = built.graph->funcType();
        const size_t promisedCount = promised != nullptr ? promised->normTypesCount() : 0;
        const size_t memberCount   = member != nullptr ? member->normTypesCount() : 0;
        // A promise with no parameter at all is not a shape this operator writes -- the cotangent
        // is always one -- so it is not a difference to read, and the count is not subtracted from.
        if (promisedCount != 0 && memberCount > promisedCount) {
            // The extra parameters stand where `transposeSignature` writes them: after the values
            // the caller passes and before the cotangent, so the promise's last parameter is the
            // first one the member adds. Their types are what the body asked for, and they are what
            // the sentence names: a reader has the source's parameter types in front of them, and a
            // count is a number they would have to look up.
            std::string carried;
            for (size_t at = promisedCount - 1; at + 1 < memberCount; ++at) {
                const camel::core::type::Type *const extra = member->normTypeAt(at);
                carried += (carried.empty() ? "" : ", ") +
                           (extra != nullptr ? extra->toString() : std::string("?"));
            }
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "transforms: transpose has no transpose for this function: the body reads the "
                "value of its `" +
                    carried + "` parameter, and the transposed function `" + given +
                    "` has no parameter for it: a parameter of the function is a linear input, so "
                    "only its cotangent is passed. Take the cotangent through `vjp`, whose cut "
                    "carries that value in its backward half's closure");
        }
    }
    auto *result = ::Function::create(
        built.program, built.graph->id(), function->tupleType(), mm::autoSpace());
    if (::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            result->tuple()->set<slot_t>(i, closure->get<slot_t>(i), function->tupleType());
        }
    }
    return toSlot<::Function *>(result);
}

} // namespace

const std::vector<oper_group_ptr_t> &transposeOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create("transpose", {{"transforms:transpose", std::make_shared<TransposeResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> transposeOperatorKernels() {
    return {
        {"transpose", &transposeKernel},
    };
}

} // namespace camel::transforms