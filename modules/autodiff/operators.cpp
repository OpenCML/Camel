// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Operators of the autodiff module (see operators.h).
 */

#include "operators.h"

#include "pullback.h"

#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

namespace camel::autodiff {

namespace {

namespace mm = camel::core::mm;
using ::ModifierSet;
using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::SemanticDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::toSlot;
using type::FunctionType;
using type::Type;
using type::type_vec_t;
using type::TypeCode;

/// grad and with_grad: (f) => the gradient function of f.
class GradResolver final : public type::FuncTypeResolver {
  public:
    explicit GradResolver(bool withValue) : withValue_(withValue) {}

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        const auto gradient = signatureOf(with, norm);
        if (!gradient) {
            return std::nullopt;
        }
        return FunctionType::create({}, {{norm[0], false}}, gradient->functionType);
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, type::static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        (void)normStatics;
        (void)modifiers;
        if (!with.empty() || norm.size() != 1 || norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        // The whole reason, naming the condition that failed; reported as NotDifferentiable.
        return differentiationRefusal(tt::as_ptr<FunctionType>(norm[0]));
    }

    std::optional<SemanticDiag> rejectionDiag() const override {
        return SemanticDiag::NotDifferentiable;
    }

    std::string signature() const override {
        return withValue_
                   ? "(f: <W...>(N...) => float) => <W...>(N...) => (float, gradient of W0)"
                   : "(f: <W...>(N...) => float) => <W...>(N...) => gradient of W0";
    }

  private:
    std::optional<GradientSignature>
    signatureOf(const type_vec_t &with, const type_vec_t &norm) const {
        if (!with.empty() || norm.size() != 1 || norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        return gradientSignature(tt::as_ptr<FunctionType>(norm[0]), withValue_);
    }

    bool withValue_;
};

/// stop_gradient: (x: T) => T is declared in `stdlib/autodiff.cml` (handover H19, step 10;
/// H165); the kernel below and the traits in `AutodiffModule::load` stay here.
slot_t gradFunction(ArgsView &norm, Context &ctx, bool withValue) {
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "autodiff: grad needs a function value");
    }
    const auto gradient = buildGradientGraph(
        ctx.shared_from_this(),
        function->program(),
        function->graph(),
        withValue);
    // The gradient graph keeps f's closure: it reads the same captured values.
    auto *result = ::Function::create(
        gradient.program,
        gradient.graph->id(),
        function->tupleType(),
        mm::autoSpace());
    if (::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            result->tuple()->set<slot_t>(i, closure->get<slot_t>(i), function->tupleType());
        }
    }
    return toSlot<::Function *>(result);
}

slot_t gradKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    return gradFunction(norm, ctx, false);
}

slot_t valueAndGradKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    return gradFunction(norm, ctx, true);
}

slot_t stopGradientKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return norm.get<slot_t>(0);
}

} // namespace

const std::vector<oper_group_ptr_t> &operatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create("grad", {{"autodiff:grad", std::make_shared<GradResolver>(false)}}),
        OperatorGroup::create(
            "with_grad",
            {{"autodiff:with_grad", std::make_shared<GradResolver>(true)}}),
        // `stop_gradient` is not here: its signature is declared in `stdlib/autodiff.cml`
        // (handover H19, step 10; H165), and its kernel and traits stay in this module.
    };
    return groups;
}

std::unordered_map<std::string, operator_t> operatorKernels() {
    return {
        {"grad", &gradKernel},
        {"with_grad", &valueAndGradKernel},
        {"stop_gradient", &stopGradientKernel},
    };
}

void registerOperatorMetadata() {
    // The transformations take a function: what a call does is what that function does, and
    // std::macro rewrites the call away before anything runs. Declared, as every operator
    // must be (design principles 3.3), as not pure.
    camel::core::OperatorTable::instance().declare(
        "autodiff",
        {{"grad", false}, {"with_grad", false}});
    // grad and with_grad differentiate the graph of the function value they receive: on a
    // back end that lowered loops in a private copy they are handed the program's own graph, so
    // they refuse a loop there as everywhere else (OperatorTraits::readsGraphs; op-semantics
    // 11.4). A custom rule is a declaration tag (`#vjp(rule)`), not an operator of this module.
    for (const char *name : {"autodiff:grad", "autodiff:with_grad"}) {
        camel::core::OperatorTable::instance().set(
            name, {.pure = false, .readsGraphs = true, .macro = true});
    }
    camel::core::OperatorTable::instance().set(
        "autodiff:stop_gradient",
        {.pure = true, .elementwise = false});
    camel::core::DerivativeRegistry::instance().setRule(
        "autodiff:stop_gradient",
        camel::core::noGradient);
}

} // namespace camel::autodiff
