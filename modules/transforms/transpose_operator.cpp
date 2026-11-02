// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose_operator.h"

#include "readings.h"
#include "transpose.h"

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
        if (function->withTypesCount() != 0) {
            return std::string(
                "`transpose` takes a function without `with` parameters: the transform builds a "
                "graph of its own and a captured value has no port to enter it by");
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

/// transpose(f) at run time: the new graph, as a function value. Nothing of `f` is changed, and the
/// value keeps `f`'s closure.
slot_t transposeKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: transpose needs a function value");
    }
    const TransposeGraph built = buildTransposeGraph(
        ctx.shared_from_this(), function->program(), function->graph(), readings());
    if (built.graph == nullptr) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError, "transforms: transpose has no transpose for this function");
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