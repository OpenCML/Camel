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

#include "readings.h"
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

/// The type of `split(f)` for a function of this type: the call answers the forward half, whose own
/// call answers `(result, lin)` with `lin` an ordinary function of the tangents. nullptr when the
/// function is outside what the step takes. The residuals are `lin`'s closure, so no position in this
/// signature states them: with no graph there is nothing to name, and the count a reading states comes
/// from the graph (`residuals=`).
FunctionType *splitOperatorSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    const SplitHalves halves = splitHalvesWith(function, nullptr);
    if (halves.pair == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(const_cast<Type *>(halves.pair));
    return factory.build();
}

/// split: a lifted function in, its forward half out. `f`'s every parameter is `Lift<T> = (T, Tangent)`
/// and its result is `(result, tangent)`, so the forward half takes the values and answers
/// `(result, lin)`, where `lin` is the backward half closed over the residuals and takes the tangents.
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
        // The reason only: no graph, so `lin` captures nothing and there is nothing to name.
        const SplitHalves halves = splitHalvesWith(tt::as_ptr<FunctionType>(norm[0]), nullptr);
        if (!halves.why.empty()) {
            return halves.why;
        }
        return std::nullopt;
    }

    /// The text is the one the declaration writes (`stdlib/signatures/transforms.cml`), and it is used
    /// for one thing: naming the overload in a refusal that lists what the operator accepts. No
    /// position in it states the residuals -- they are `lin`'s closure, and the count the step prints
    /// comes from the graph (`residuals=`).
    std::string signature() const override {
        return "(f: (float, float) => (float, float)) => (float, float) => (float, (float) => float)";
    }

    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// split(f) at run time: the forward half, as a function value. `f`'s graph is read, never changed.
slot_t splitKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: split needs a function value");
    }
    const SplitGraph built = buildSplitGraph(
        ctx.shared_from_this(), function->program(), function->graph(), readings());
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
    auto *forwardFunction = ::Function::create(
        built.program, built.forward->id(), function->tupleType(), mm::autoSpace());
    if (::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            forwardFunction->tuple()->set<slot_t>(
                i, closure->get<slot_t>(i), function->tupleType());
        }
    }
    // The answer is the forward half alone: `fwd(x)` is `(y, lin)`, and `lin` is built inside that
    // graph, closed over the residuals the call computes (author, 2026-11-02 05:10). The backward
    // graph is reachable only through that `lin`, which is why the call hands out no second value.
    printSplitCallReading(readings(), function->graph()->name());
    return toSlot<::Function *>(forwardFunction);
void registerSplitOperatorMetadata() {
    camel::core::OperatorTable::instance().declare("transforms", {{"split", false}});
    // The step reads the graph of the function value it receives -- that is the whole transform --
    // so it refuses a loop there (OperatorTraits::readsGraphs).
    camel::core::OperatorTable::instance().set(
        "transforms:split", {.pure = false, .readsGraphs = true});
    // How a call of it is written: `split(f)`, one function and no `with` parameters. A Camel
    // declaration cannot spell the signature -- the result is the function's two halves, a type no
    // signature writes -- so the call shape is recorded here, where the operator is declared,
    // rather than in a signature invented to make the printer pass (P1-13).
    camel::core::OperatorTable::instance().declareCallForm(
        "transforms:split", {.name = "split", .minArgs = 1, .withCount = 0, .declaredByModule = true});
}

} // namespace camel::transforms
