// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `vmap(f)` as an operator of the `transforms` module (see vmap.h).
 *
 * The wiring is `jvp`'s, copied rather than shared: a type resolver, a kernel and the two
 * registrations (module.cpp exports the group and declares the operator). What is *not* copied is
 * the work: the kernel calls `buildVmapGraph` (vmap.cpp), which is the one place that knows what
 * `reinterp<vmap>(f)` is, and answers with the function value naming the graph it built.
 */

#include "vmap_operator.h"

#include "vmap.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
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

/// The type of `vmap(f)` for a function of this type: `(f: F) => Vmap<F>`, or nullptr when the
/// function is outside what the transform builds. The parameter is named, so the type prints the way
/// the declaration writes it.
FunctionType *vmapOperatorSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    if (function->withTypesCount() != 0 || function->normTypesCount() != 1) {
        return nullptr;
    }
    FunctionType *lifted = vmapSignature(function);
    if (lifted == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(lifted);
    return factory.build();
}

/// vmap: (f: (f32) => f32) => (f32[]) => f32[]. The lifted function takes the batch of rows and
/// answers the batch of results, which is `Lift<T> = RowsOf<T>` in and out.
class VmapResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm) const override {
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        FunctionType *lifted = vmapOperatorSignature(tt::as_ptr<FunctionType>(norm[0]));
        if (lifted == nullptr) {
            return std::nullopt;
        }
        return lifted;
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
        if (function->withTypesCount() != 0) {
            return std::string(
                "`vmap` takes a function without `with` parameters: the transform copies the body "
                "into a new graph, and a captured value has no port to enter it by");
        }
        if (function->normTypesCount() != 1) {
            return std::string("`vmap` takes a function of exactly one parameter");
        }
        return std::nullopt;
    }

    /// The text is the one a declaration writes (`stdlib/signatures/transforms.cml`).
    std::string signature() const override {
        return "(f: (f32) => f32) => (f32[]) => f32[]";
    }

    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// vmap(f) at run time: the new graph, as a function value. `f`'s graph is read, never changed.
slot_t vmapKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: vmap needs a function value");
    }
    const VmapGraph built = buildVmapGraph(
        ctx.shared_from_this(), function->program(), function->graph(), std::cout);
    if (built.graph == nullptr) {
        if (built.refusedOrigin == camel::source::kInvalidOriginId) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "transforms: vmap has no batching for this function");
        }
        const std::string why =
            built.refusedConstruct.empty()
                ? std::format(
                      "transforms: vmap has no rule for `{}` ({})", built.refusedUri,
                      built.refusedWhy)
                : std::format(
                      "transforms: vmap does not batch {} yet ({})", built.refusedConstruct,
                      built.refusedWhy);
        throw camel::core::error::RuntimeFault(
            RuntimeDiag::RuntimeError, {why}, "", nullptr,
            camel::core::error::ExecutionSiteHint{
                .cachedOrigin = built.refusedOrigin, .keepRaiseSite = true});
    }
    const std::string spelled  = VmapResolver{}.signature();
    const auto resolved        = VmapResolver{}.resolve({}, type_vec_t{function->graph()->funcType()});
    const std::string produced = resolved ? (*resolved)->toString() : std::string("-");
    const std::string lifted =
        (resolved && (*resolved)->hasExitType()) ? (*resolved)->exitType()->toString()
                                                 : std::string("-");
    const std::string handedBack =
        built.graph->funcType() != nullptr ? built.graph->funcType()->toString() : "-";
    std::cout << "REINTERP vmap op signature=" << spelled << " resolved=" << produced
              << " lifted=" << lifted << " graph=" << handedBack
              << " same=" << (lifted == handedBack ? "yes" : "no") << " asked=" << built.asked
              << " found=" << built.found << '\n';
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

const std::vector<oper_group_ptr_t> &vmapOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create("vmap", {{"transforms:vmap", std::make_shared<VmapResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> vmapOperatorKernels() {
    return {
        {"vmap", &vmapKernel},
    };
}

void registerVmapOperatorMetadata() {
    camel::core::OperatorTable::instance().declare("transforms", {{"vmap", false}});
    camel::core::OperatorTable::instance().set(
        "transforms:vmap", {.pure = false, .readsGraphs = true});
}

} // namespace camel::transforms