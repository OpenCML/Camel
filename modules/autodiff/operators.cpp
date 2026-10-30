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
using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::SemanticDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::toSlot;
using type::FunctionType;
using type::Type;
using type::type_vec_t;
using type::TypeCode;

/// grad and with_grad: (f) => the gradient function of f, and (f, has_aux) => the same for a
/// function whose result is the pair `(value, aux)` (see stdlib/signatures/autodiff.cml).
/// `argnums` is not a parameter: what is differentiated is the first `with` parameter (design
/// principles 9.9), and the author's ruling keeps that the one way to choose it.
class GradResolver final : public type::FuncTypeResolver {
  public:
    explicit GradResolver(bool withValue, bool hasAux) : withValue_(withValue), hasAux_(hasAux) {}

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm) const override {
        const auto gradient = signatureOf(with, norm, /*statesSplit=*/hasAux_);
        if (!gradient) {
            return std::nullopt;
        }
        return FunctionType::create({}, {{norm[0], false}}, gradient->functionType);
    }

    /// `has_aux` is a constant and its value has to be `true`: the pair is split, or it is not
    /// and the plain form is what the writer means (design principles: a rule is one signature,
    /// so the shape of the result may not follow a value that is only known when the program
    /// runs). A value the compiler cannot read, and a `false`, are refused with the reason.
    std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, type::static_args_t statics) const override {
        if (hasAux_ && staticBool(statics) != std::optional<bool>(true)) {
            return std::nullopt;
        }
        return resolve(with, norm);
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, type::static_args_t normStatics) const override {
        if (!with.empty() || norm.empty() || norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        if (norm.size() != expectedNormCount() ||
            (hasAux_ && norm[1]->code() != TypeCode::Bool)) {
            return std::nullopt;
        }
        if (hasAux_) {
            const std::optional<bool> constant = staticBool(normStatics);
            if (!constant) {
                return std::string(
                    "`has_aux` must be a constant `true`: the pair the function returns is split, "
                    "and whether it is cannot follow a value the program computes");
            }
            if (!*constant) {
                return std::string(
                    "`has_aux` is false, which is the plain transform: write `grad(f)` (or "
                    "`with_grad(f)`) without the argument instead");
            }
            return differentiationRefusal(tt::as_ptr<FunctionType>(norm[0]), true);
        }
        // The whole reason, naming the condition that failed; reported as NotDifferentiable.
        return differentiationRefusal(tt::as_ptr<FunctionType>(norm[0]), false);
    }

    std::optional<SemanticDiag> rejectionDiag() const override {
        return SemanticDiag::NotDifferentiable;
    }

    /// What a named argument binds to: `f`, and -- of the `has_aux` form -- `has_aux`. A rule
    /// whose signature is computed has no signature object to answer from, and a call to
    /// `grad(f, argnums: 0)` must be refused as the unknown parameter it is, not bound to a
    /// position this rule does not name.
    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = hasAux_ ? std::vector<std::string>{"f", "has_aux"}
                       : std::vector<std::string>{"f"};
        return true;
    }

    std::string signature() const override {
        if (!hasAux_) {
            return withValue_
                       ? "(f: <W...>(N...) => float) => <W...>(N...) => (float, gradient of W0)"
                       : "(f: <W...>(N...) => float) => <W...>(N...) => gradient of W0";
        }
        return withValue_
                   ? "(f: <W...>(N...) => (float, aux), has_aux: bool) => <W...>(N...) => ((float, "
                     "aux), gradient of W0)"
                   : "(f: <W...>(N...) => (float, aux), has_aux: bool) => <W...>(N...) => "
                     "(gradient of W0, aux)";
    }

  private:
    size_t expectedNormCount() const { return hasAux_ ? 2 : 1; }

    /// Whether the `has_aux` argument of the call is a constant the compiler can read, and which
    /// one. It is the second parameter's value, and it is what picks the shape of the result.
    static std::optional<bool> staticBool(type::static_args_t statics) {
        if (statics.size() < 2 || !statics[1].value.has_value()) {
            return std::nullopt;
        }
        return camel::core::rtdata::fromSlot<bool>(*statics[1].value);
    }

    std::optional<GradientSignature> signatureOf(
        const type_vec_t &with, const type_vec_t &norm, bool split) const {
        if (!with.empty() || norm.size() != expectedNormCount() ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        if (!hasAux_) {
            return gradientSignature(tt::as_ptr<FunctionType>(norm[0]), withValue_, false);
        }
        if (norm[1]->code() != TypeCode::Bool) {
            return std::nullopt;
        }
        return gradientSignature(tt::as_ptr<FunctionType>(norm[0]), withValue_, split);
    }

    bool withValue_;
    bool hasAux_;
};

/// stop_gradient: (x: T) => T is declared in `stdlib/autodiff.cml`; the kernel below and the
/// traits in `AutodiffModule::load` stay here.
slot_t gradFunction(ArgsView &norm, Context &ctx, bool withValue, bool hasAux) {
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "autodiff: grad needs a function value");
    }
    const auto gradient = buildGradientGraph(
        ctx.shared_from_this(),
        function->program(),
        function->graph(),
        withValue,
        hasAux);
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
    return gradFunction(norm, ctx, false, false);
}

slot_t valueAndGradKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    return gradFunction(norm, ctx, true, false);
}

/// The `has_aux` forms: the function's result is the pair `(value, aux)`, and the transform gives
/// `(gradient, aux)` for `grad`, `((value, aux), gradient)` for `with_grad`.
slot_t gradAuxKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    return gradFunction(norm, ctx, false, true);
}

slot_t valueAndGradAuxKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    return gradFunction(norm, ctx, true, true);
}

slot_t stopGradientKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return norm.get<slot_t>(0);
}

} // namespace

const std::vector<oper_group_ptr_t> &operatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        // `grad` and `with_grad` take one parameter, and a second form of each takes `has_aux`
        // as well: the transform of a function whose result is the pair `(value, aux)`. The two
        // are two rules of one name, so a call is resolved by its arguments -- `grad(f)` and
        // `grad(f, has_aux: true)` -- and neither form can be reached by accident. `argnums` is
        // deliberately absent: the differentiated value is the first `with` parameter (design
        // principles 9.9), and a call that names `argnums` is refused as the unknown parameter it is.
        OperatorGroup::create(
            "grad",
            {{"autodiff:grad", std::make_shared<GradResolver>(false, false)},
             {"autodiff:grad_aux", std::make_shared<GradResolver>(false, true)}}),
        OperatorGroup::create(
            "with_grad",
            {{"autodiff:with_grad", std::make_shared<GradResolver>(true, false)},
             {"autodiff:with_grad_aux", std::make_shared<GradResolver>(true, true)}}),
        // `stop_gradient` is not here: its signature is declared in `stdlib/autodiff.cml`,
        // and its kernel and traits stay in this module.
    };
    return groups;
}

std::unordered_map<std::string, operator_t> operatorKernels() {
    return {
        {"grad", &gradKernel},
        {"grad_aux", &gradAuxKernel},
        {"with_grad", &valueAndGradKernel},
        {"with_grad_aux", &valueAndGradAuxKernel},
        {"stop_gradient", &stopGradientKernel},
    };
}

void registerOperatorMetadata() {
    // The transformations take a function: what a call does is what that function does, and
    // std::macro rewrites the call away before anything runs. Declared, as every operator
    // must be (design principles 3.3), as not pure.
    camel::core::OperatorTable::instance().declare(
        "autodiff",
        {{"grad", false}, {"grad_aux", false}, {"with_grad", false}, {"with_grad_aux", false}});
    // grad and with_grad differentiate the graph of the function value they receive: on a
    // back end that lowered loops in a private copy they are handed the program's own graph, so
    // they refuse a loop there as everywhere else (OperatorTraits::readsGraphs; op-semantics
    // 11.4). A custom rule is a declaration tag (`#vjp(rule)`), not an operator of this module.
    for (const char *name :
         {"autodiff:grad", "autodiff:grad_aux", "autodiff:with_grad", "autodiff:with_grad_aux"}) {
        camel::core::OperatorTable::instance().set(
            name, {.pure = false, .readsGraphs = true, .macro = true});
    }
    camel::core::OperatorTable::instance().set(
        "autodiff:stop_gradient",
        {.pure = true, .elementwise = false});
    // `stop_gradient` is declared in `stdlib/autodiff.cml`, and its clause (`pure`) is what the
    // declaration says it touches: nothing. A native operator has to say the same thing here, so
    // the two are compared instead of one being taken on faith (design principles 3.3).
    camel::core::OperatorTable::instance().declareResources(
        "autodiff",
        {{"stop_gradient", camel::core::sem::ResourceSpec{}}});
    camel::core::DerivativeRegistry::instance().setRule(
        "autodiff:stop_gradient",
        camel::core::noGradient);
}

} // namespace camel::autodiff
