/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
using ::Modifier;
using ::ModifierSet;
using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::toSlot;
using type::FunctionType;
using type::Type;
using type::type_vec_t;
using type::TypeCode;

/// grad and value_and_grad: (f) => the gradient function of f.
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
        return FunctionType::create(
            {},
            {{norm[0], false}},
            gradient->functionType,
            Modifier::Macro);
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, type::static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        (void)normStatics;
        (void)modifiers;
        if (!with.empty() || norm.size() != 1 || norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        return std::format(
            "cannot differentiate a function of type {}: it must return a float and take a "
            "parameter that has a gradient (a float, a tensor, or a struct or tuple of them)",
            norm[0]->toString());
    }

    std::string signature() const override {
        return withValue_ ? "(f: <W...>(N...) => float) => <W...>(N...) => (float, gradient)"
                          : "(f: <W...>(N...) => float) => <W...>(N...) => gradient";
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

/// stop_gradient: (x: T) => T.
class IdentityResolver final : public type::FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        if (!with.empty() || norm.size() != 1) {
            return std::nullopt;
        }
        return FunctionType::create({}, {{norm[0], false}}, norm[0]);
    }

    std::string signature() const override { return "(x: T) => T"; }
};

slot_t gradFunction(ArgsView &norm, Context &ctx, bool withValue) {
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "autodiff: grad needs a function value");
    }
    auto *graph = buildGradientGraph(ctx.shared_from_this(), function->graph(), withValue);
    // The gradient graph keeps f's closure: it reads the same captured values.
    auto *result = ::Function::create(graph, function->tupleType(), mm::autoSpace());
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
            "value_and_grad",
            {{"autodiff:value_and_grad", std::make_shared<GradResolver>(true)}}),
        OperatorGroup::create(
            "stop_gradient",
            {{"autodiff:stop_gradient", std::make_shared<IdentityResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> operatorKernels() {
    return {
        {"grad", &gradKernel},
        {"value_and_grad", &valueAndGradKernel},
        {"stop_gradient", &stopGradientKernel},
    };
}

void registerOperatorMetadata() {
    camel::core::OperatorTraitsRegistry::instance().set(
        "autodiff:stop_gradient",
        {.pure = true, .elementwise = false});
    camel::core::DerivativeRegistry::instance().setRule(
        "autodiff:stop_gradient",
        camel::core::noGradient);
}

} // namespace camel::autodiff
