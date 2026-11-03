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
        if (function->exitType() != camel::core::type::Type::Float64()) {
            return std::string(
                "`transpose` takes a function that returns a `float`: the cotangent of the "
                "result is what goes in");
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
/// Only the cause is answered here -- the prefix `transpose has no transpose for this function` stays
/// where the raise site writes it, because it is what a reader greps for and what the plans pin.
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
    case TransposeRefusalCause::State:
        return "a loop carries its state out of itself, and that shape has no transpose";
    case TransposeRefusalCause::StateUnread:
        return "the loop is the whole answer and the value its state starts from answers no parameter, "
               "so the cotangent of that value has no parameter to land on";
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
        return "the rule table has no rule for a step of this body";
    case TransposeRefusalCause::Condition:
        return "a bool condition has no cotangent space, so it carries no derivative";
    case TransposeRefusalCause::Signature:
    case TransposeRefusalCause::Exit:
        return "the graph's shape disagrees with the signature the call was written against";
    // No program in this tree reaches the shapes below yet, so each keeps the sentence that names
    // the shape instead of a sentence written for a cause nobody has read; when one becomes
    // reachable, give it the sentence it stands for.
    case TransposeRefusalCause::Arity:
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
        ctx.shared_from_this(), function->program(), function->graph(), why);
    readings() << why.str();
    if (built.graph == nullptr) {
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