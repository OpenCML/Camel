// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `split(f)` as an operator of the `transforms` module (see split_operator.h).
 *
 * The wiring is `vmap`'s, copied rather than shared: a type resolver, a kernel and the two
 * registrations (module.cpp exports the group and declares the operator). What is *not* copied is
 * the work: the kernel calls `buildSplitGraph` (split.cpp), which is the one place that knows what
 * the two halves of a lifted function are, and answers with the pair of function values naming the
 * graphs it built.
 */

#include "split_operator.h"

#include "split.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
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
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::type_vec_t;
using camel::core::type::TypeCode;

/// The type of `split(f)` for a function of this type: the call, whose result is the pair of the two
/// halves. nullptr when the function is outside what the step takes.
FunctionType *splitOperatorSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    const SplitHalves halves = splitHalvesOf(function);
    if (halves.pair == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(const_cast<TupleType *>(halves.pair));
    return factory.build();
}

/// split: a lifted function in, its two halves out. `f`'s every parameter is `Lift<T> = (T, Tangent)`
/// and its result is `(result, tangent)`, so the forward half takes the values and answers
/// `(result, residuals)` while the backward half takes the residuals and the tangents and answers
/// the tangent.
class SplitResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm) const override {
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        FunctionType *signature = splitOperatorSignature(tt::as_ptr<FunctionType>(norm[0]));
        if (signature == nullptr) {
            return std::nullopt;
        }
        return signature;
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm,
        type::static_args_t normStatics) const override {
        (void)normStatics;
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        const SplitHalves halves = splitHalvesOf(tt::as_ptr<FunctionType>(norm[0]));
        if (!halves.why.empty()) {
            return halves.why;
        }
        return std::nullopt;
    }

    /// The text is the one a declaration writes (`stdlib/signatures/transforms.cml`), for the shape
    /// the step computes at the type it is given.
    std::string signature() const override {
        return "(f: (float, float) => (float, float)) => ((float) => (float, ()), ((), float) => "
               "float)";
    }

    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// split(f) at run time: the pair of the two graphs, as two function values in one tuple. `f`'s
/// graph is read, never changed.
slot_t splitKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: split needs a function value");
    }
    const SplitGraph built = buildSplitGraph(
        ctx.shared_from_this(), function->program(), function->graph(), std::cout);
    if (built.forward == nullptr || built.backward == nullptr) {
        if (built.refusedOrigin == camel::source::kInvalidOriginId) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                built.refusedWhy.empty() ? "transforms: split has no rule for this function"
                                         : "transforms: split does not build this function (" +
                                               built.refusedWhy + ")");
        }
        const std::string why =
            built.refusedConstruct.empty()
                ? std::format(
                      "transforms: split has no rule for `{}` ({})", built.refusedUri,
                      built.refusedWhy)
                : std::format(
                      "transforms: split does not copy {} yet ({})", built.refusedConstruct,
                      built.refusedWhy);
        throw camel::core::error::RuntimeFault(
            RuntimeDiag::RuntimeError, {why}, "", nullptr,
            camel::core::error::ExecutionSiteHint{
                .cachedOrigin = built.refusedOrigin, .keepRaiseSite = true});
    }
    const SplitHalves halves = splitHalvesOf(function->graph()->funcType());
    auto *forwardFunction    = ::Function::create(
        built.program, built.forward->id(), function->tupleType(), mm::autoSpace());
    auto *backwardFunction = ::Function::create(
        built.program, built.backward->id(), function->tupleType(), mm::autoSpace());
    if (::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            forwardFunction->tuple()->set<slot_t>(
                i, closure->get<slot_t>(i), function->tupleType());
            backwardFunction->tuple()->set<slot_t>(
                i, closure->get<slot_t>(i), function->tupleType());
        }
    }
    auto *pair = ::Tuple::create(2, mm::autoSpace());
    pair->set<slot_t>(0, toSlot<::Function *>(forwardFunction), halves.pair);
    pair->set<slot_t>(1, toSlot<::Function *>(backwardFunction), halves.pair);
    const std::string pairText =
        halves.pair != nullptr ? halves.pair->toString() : std::string("-");
    std::cout << "REINTERP split op signature=" << SplitResolver{}.signature()
              << " resolved=" << pairText << " residuals=" << built.residuals << '\n';
    return toSlot<::Tuple *>(pair);
}

} // namespace

const std::vector<oper_group_ptr_t> &splitOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create("split", {{"transforms:split", std::make_shared<SplitResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> splitOperatorKernels() {
    return {
        {"split", &splitKernel},
    };
}

void registerSplitOperatorMetadata() {
    camel::core::OperatorTable::instance().declare("transforms", {{"split", false}});
    // The step reads the graph of the function value it receives -- that is the whole transform --
    // so it refuses a loop there (OperatorTraits::readsGraphs).
    camel::core::OperatorTable::instance().set(
        "transforms:split", {.pure = false, .readsGraphs = true});
}

} // namespace camel::transforms