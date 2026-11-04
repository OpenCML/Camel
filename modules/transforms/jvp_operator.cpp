// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `jvp(f)` as an operator of the `transforms` module (see jvp_operator.h).
 *
 * This is the wiring `camel::autodiff`'s `grad` has, copied rather than shared: a type resolver,
 * a kernel and the two registrations. What is *not* copied is the work: the kernel calls
 * `buildJvpGraph` (jvp.cpp), which is the one place that knows what `reinterp<jvp>(f)` is, and
 * answers with the function value naming the graph it built -- the author's ruling (a824031aa)
 * that the transform produces a new function and leaves `f` untouched.
 */

#include "jvp_operator.h"

#include "jvp.h"
#include "readings.h"
#include "transpose_operator.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <format>
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
using camel::core::type::Type;
using camel::core::type::type_vec_t;
using camel::core::type::TypeCode;

/// The type of `jvp(f)` for a function of this type: `(f: F) => jvpSignature(F)`, or nullptr when
/// the function has no forward transform. The parameter is named, so the type prints the way the
/// declaration writes it (`(f: (float) => float) => (float, float) => (float, float)`); the
/// declaration's names and the rule's names are compared position by position, so they have to be
/// one spelling.
FunctionType *jvpOperatorSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    // What `buildJvpGraph` can build, and nothing else: it copies one body and gives every parameter
    // of that body a pair of ports, so it takes any number of ordinary parameters (at least one).
    // A `with` parameter is not a reason to refuse: the lift writes that parameter a port of its own
    // and lists it among the lifted graph's `with` ports, so the captured value enters by a port and
    // the body reads it as it did (design principles 9.9: what `grad` differentiates is the first
    // `with` parameter -- the weights -- and `vjp` is `transpose(split(jvp(f)))`, so a `jvp` that
    // refuses every function carrying a weight makes a gradient of the weights impossible to build
    // rather than merely unwritten). Its tangent is the zero of its type, which is what a captured
    // value's tangent is in forward mode, so the lift adds no tangent port for it. A function
    // outside that shape is refused where it is written, not left to fail when the call runs.
    if (function->normTypesCount() == 0) {
        return nullptr;
    }
    FunctionType *lifted = jvpSignature(function);
    if (lifted == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(lifted);
    return factory.build();
}

/// jvp: (f: (float) => float) => (float, float) => (float, float). One pair `(value, tangent)` in
/// per parameter of `f` and the pair `(value, tangent)` out (jvp.h); the seed is the tangent the
/// caller passes, so `jvp(f)(2.0, 1.0)` is the derivative in the direction 1, and for a function of
/// two parameters `jvp(f)(x, dx, y, dy)` is the derivative in the direction `(dx, dy)`.
class JvpResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm) const override {
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        FunctionType *lifted = jvpOperatorSignature(tt::as_ptr<FunctionType>(norm[0]));
        if (lifted == nullptr) {
            return std::nullopt;
        }
        return lifted;
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, type::static_args_t normStatics) const override {
        (void)normStatics;
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        const auto *function = tt::as_ptr<FunctionType>(norm[0]);
        if (function->normTypesCount() == 0) {
            return std::string(
                "`jvp` takes a function of at least one ordinary parameter: a lifted function's "
                "parameters are the value and its tangent, and there has to be one to lift");
        }
        // The only question asked of a parameter is the one design principles 9.9 asks of every
        // value -- does its type have a tangent space -- and the answer is the type's own
        // (`tangentSpaceOf`, `Tangent<T>`), never this transform's state. The two branches that
        // stood here said more than the law does: one refused every parameter that was not a
        // `float` ("which this transform does not lift yet") and one refused every result that was
        // not a `float`. Neither could be reached -- `jvpSignature` already builds the pair out of
        // the tangent space, so the call resolved and this text was never asked for -- but a
        // rejection written outside the declaration is a second law, and this one said a supported
        // thing was unsupported. Measured on `jvp(f)(...)`: `f32` gives `(9.0, 6.0)`, a struct
        // `{a, b}` gives `(6.0, 3.0)`, `float[]` gives `(6.0, 2.0)`.
        for (size_t i = 0; i < function->normTypesCount(); ++i) {
            const camel::core::type::Type *parameter = function->normTypeAt(i);
            const camel::core::TangentSpaceAnswer space =
                camel::core::tangentSpaceOf(parameter);
            if (!space.hasSpace()) {
                return std::format(
                    "`jvp` takes a function whose every parameter has a tangent space, and "
                    "parameter {} is a `{}`, which has none",
                    i,
                    parameter->toString());
            }
        }
        const camel::core::TangentSpaceAnswer resultSpace =
            camel::core::tangentSpaceOf(function->exitType());
        if (!resultSpace.hasSpace()) {
            return std::format(
                "`jvp` takes a function whose result has a tangent space, and this one's result "
                "is a `{}`, which has none: the lifted signature answers the pair "
                "(value, tangent)",
                function->exitType() != nullptr ? function->exitType()->toString() : "()");
        }
        return std::nullopt;
    }

    /// The law, not one instance of it. What `jvp(f)` is for a given `f` is `jvpSignature`'s answer
    /// (`modules/transforms/jvp.cpp`), and the declaration the module publishes says the same thing
    /// in prose (`stdlib/signatures/transforms.cml`, `Jvp<F>`). This string used to write one
    /// instance -- `(f: (float) => float) => (float, float) => (float, float)` -- which stopped
    /// being true as soon as any parameter that is not a `float` was accepted, and a diagnostic that
    /// states a false type is worse than one that states the rule (author, A2 of 2026-11-03: the type
    /// follows the declaration, and the declaration is the only source).
    std::string signature() const override {
        return "(f: (..) => any) => (the parameters and their tangents) => (value, tangent)";
    }

    /// `f` is the only parameter, in every form: `jvp(f: g)` has to bind to it.
    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// jvp(f) at run time: the new graph, as a function value. Nothing of `f`'s graph is changed --
/// `buildJvpGraph` reads it and reserves a member of its own group -- and the value keeps `f`'s
/// closure, because the nodes copied out of the body may read it.
slot_t jvpKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: jvp needs a function value");
    }
    // The reading is part of the interface (jvp.h): a graph the caller cannot name yet is a graph
    // nobody can check. It goes to the diagnostic side (`readings.h`), not to the program's output:
    // the same walk that explains a refusal used to land among the numbers the program printed.
    const JvpGraph built = buildJvpGraph(
        ctx.shared_from_this(), function->program(), function->graph(), readings());
    if (built.graph == nullptr) {
        if (built.refusedOrigin == camel::source::kInvalidOriginId) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "transforms: jvp has no forward transform for this function");
        }
        // The fault stands where the primitive was written, not where `jvp` was called: the hint
        // names the node's origin and keeps the raising site as a related position, so the error
        // reads `f`'s `file:line` and still tells two calls of the same `jvp` apart
        // (ExecutionSiteHint::keepRaiseSite). A refusal that only prints a reading would leave the
        // reader at the call site, guessing which operator in `f` it was.
        //
        // A named construct is refused with the sentence the language would write for it, not with
        // "this transform does not reinterpret it yet" (author, 2026-11-04 04:20): the constructs
        // that reach here are outside the rule -- a loop whose element has a tangent, a call whose
        // callee takes nothing but its `with` values -- and the rule plus the reading is the whole
        // answer, so nothing is promised for later.
        const std::string why = built.refusedConstruct.empty()
                                    ? std::format(
                                          "transforms: jvp has no forward rule for `{}` ({})",
                                          built.refusedUri, built.refusedWhy)
                                    : std::format(
                                          "transforms: jvp cannot differentiate through {} ({})",
                                          built.refusedConstruct, built.refusedWhy);
        throw camel::core::error::RuntimeFault(
            RuntimeDiag::RuntimeError, {why}, "", nullptr,
            camel::core::error::ExecutionSiteHint{
                .cachedOrigin = built.refusedOrigin, .keepRaiseSite = true});
    }
    // The spellings of the operator, side by side. `signature()` is the text a declaration writes
    // (stdlib/signatures/transforms.cml repeats it) and the type `resolve()` gives the call; the two
    // differ only inside the argument's own spelling -- the function value's parameter name and its
    // `pure` clause, which a declaration cannot write and `--verify-ops` does not compare. What has
    // to be one string, and is what the call site relies on, is the lifted half: the type the
    // operator says it returns and the type of the graph it hands back.
    printJvpCallReading(readings(), function->graph()->funcType(), built.graph->funcType());
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

void printJvpCallReading(
    std::ostream &os, FunctionType *function, const FunctionType *answered) {
    const std::string spelled = JvpResolver{}.signature();
    const auto resolved =
        function != nullptr ? JvpResolver{}.resolve({}, type_vec_t{function}) : std::nullopt;
    const std::string produced = resolved ? (*resolved)->toString() : std::string("-");
    const std::string lifted =
        (resolved && (*resolved)->hasExitType()) ? (*resolved)->exitType()->toString()
                                                 : std::string("-");
    const std::string handedBack = answered != nullptr ? answered->toString() : "-";
    os << "REINTERP jvp op signature=" << spelled
       << " resolved=" << produced << " lifted=" << lifted << " graph=" << handedBack
       << " same=" << (lifted == handedBack ? "yes" : "no") << '\n';
}

// The two steps of `vjp` (stdlib/vjp.cml) have rules of their own -- `split` in
// modules/transforms/split_operator.cpp and `transpose` in modules/transforms/transpose_operator.cpp
// -- so the stand-ins that answered "this step is not bound for this function" are gone with them:
// a step either has a rule or says it has none, and it no longer has a shape without one.
const std::vector<oper_group_ptr_t> &operatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        // One rule: `jvp` takes a function and gives its forward transform (design principles 9.9
        // put the seed in the transformed function's parameters, not in a second argument here).
        OperatorGroup::create(
            "jvp",
            {{"transforms:jvp", std::make_shared<JvpResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> operatorKernels() {
    std::unordered_map<std::string, operator_t> kernels = transposeOperatorKernels();
    kernels["jvp"] = &jvpKernel;
    return kernels;
}

void registerOperatorMetadata() {
    // It builds a graph: the call does something the compiler cannot see from the arguments alone,
    // so it is declared, as every operator must be (design principles 3.3), as not pure.
    camel::core::OperatorTable::instance().declare(
        "transforms", {{"jvp", false}, {"transpose", false}});
    // `jvp` reads the graph of the function value it receives -- that is the whole transform -- so
    // it refuses a loop there as `grad` does (OperatorTraits::readsGraphs; op-semantics 11.4).
    camel::core::OperatorTable::instance().set(
        "transforms:jvp", {.pure = false, .readsGraphs = true});
    // `transpose` builds a graph of its own from the one it is handed, for the same two reasons.
    // Its declaration is the one above, beside `jvp` and `split`: a name declared twice says nothing
    // the first one did not.
    camel::core::OperatorTable::instance().set(
        "transforms:transpose", {.pure = false, .readsGraphs = true});
    // How a call of each is written: `jvp(f)` and `transpose(f)`, one function and no `with`
    // parameters. Neither has a Camel declaration that could say so -- the result of `jvp` is the
    // forward transform of `f`, the result of `transpose` its reverse transform, and no signature
    // writes either type -- so the call shape is recorded here, where the operator is declared,
    // rather than in a signature invented to make the printer pass (P1-13: an operator with no
    // compilable declaration).
    camel::core::OperatorTable::instance().declareCallForm(
        "transforms:jvp", {.name = "jvp", .minArgs = 1, .withCount = 0, .declaredByModule = true});
    camel::core::OperatorTable::instance().declareCallForm(
        "transforms:transpose", {.name = "transpose", .minArgs = 1, .withCount = 0, .declaredByModule = true});
}

} // namespace camel::transforms
