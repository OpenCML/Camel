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

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
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
    // What `buildJvpGraph` can build, and nothing else: it copies one body, so it takes one
    // ordinary parameter, and it has no node that reads a captured value into a port, so the
    // function has no `with` parameters. A function outside that shape is refused where it is
    // written, not left to fail when the call runs.
    if (function->withTypesCount() != 0 || function->normTypesCount() != 1) {
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

/// jvp: (f: (float) => float) => (float, float) => (float, float). The lifted signature is the pair
/// `(value, tangent)` in and the pair `(value, tangent)` out (jvp.h); the seed is the tangent the
/// caller passes, so `jvp(f)(2.0, 1.0)` is the derivative in the direction 1.
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
        if (function->withTypesCount() != 0) {
            return std::string(
                "`jvp` takes a function without `with` parameters: the transform copies the body "
                "into a new graph, and a captured value has no port to enter it by");
        }
        if (function->normTypesCount() != 1) {
            return std::string("`jvp` takes a function of exactly one parameter");
        }
        if (function->normTypeAt(0) != Type::Float64() || function->exitType() != Type::Float64()) {
            return std::string(
                "`jvp` takes a function of one `float` parameter that returns a `float`: the "
                "lifted signature is `(float, float) => (float, float)`");
        }
        return std::nullopt;
    }

    /// The text is the one a declaration writes (`stdlib/signatures/transforms.cml`), and it is
    /// the same string the resolved type prints: `((float) => float)` in, the lifted function out.
    std::string signature() const override {
        return "(f: (float) => float) => (float, float) => (float, float)";
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
    // nobody can check. It goes to the program's own output, as the pass's reading does.
    const JvpGraph built = buildJvpGraph(
        ctx.shared_from_this(), function->program(), function->graph(), std::cout);
    if (built.graph == nullptr) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "transforms: jvp has no forward transform for this function");
    }
    // The spellings of the operator, side by side. `signature()` is the text a declaration writes
    // (stdlib/signatures/transforms.cml repeats it) and the type `resolve()` gives the call; the two
    // differ only inside the argument's own spelling -- the function value's parameter name and its
    // `pure` clause, which a declaration cannot write and `--verify-ops` does not compare. What has
    // to be one string, and is what the call site relies on, is the lifted half: the type the
    // operator says it returns and the type of the graph it hands back.
    const std::string spelled = JvpResolver{}.signature();
    const auto resolved = JvpResolver{}.resolve({}, type_vec_t{function->graph()->funcType()});
    const std::string produced = resolved ? (*resolved)->toString() : std::string("-");
    const std::string lifted =
        (resolved && (*resolved)->hasExitType()) ? (*resolved)->exitType()->toString()
                                                 : std::string("-");
    const std::string handedBack =
        built.graph->funcType() != nullptr ? built.graph->funcType()->toString() : "-";
    std::cout << "REINTERP jvp op signature=" << spelled << " resolved=" << produced
              << " lifted=" << lifted << " graph=" << handedBack
              << " same=" << (lifted == handedBack ? "yes" : "no") << '\n';
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
    return {
        {"jvp", &jvpKernel},
    };
}

void registerOperatorMetadata() {
    // It builds a graph: the call does something the compiler cannot see from the arguments alone,
    // so it is declared, as every operator must be (design principles 3.3), as not pure.
    camel::core::OperatorTable::instance().declare("transforms", {{"jvp", false}});
    // `jvp` reads the graph of the function value it receives -- that is the whole transform -- so
    // it refuses a loop there as `grad` does (OperatorTraits::readsGraphs; op-semantics 11.4).
    camel::core::OperatorTable::instance().set(
        "transforms:jvp", {.pure = false, .readsGraphs = true});
}

} // namespace camel::transforms