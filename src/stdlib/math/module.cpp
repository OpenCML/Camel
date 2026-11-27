// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/data/primary.h"
#include "camel/core/derivative.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "numeric.h"
#include "operators.h"
#include "semantics.h"

#include <array>
#include <limits>
#include <numbers>
#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::core::data::DoubleData;

namespace {
// sqrt accepts integer primals as well, but only floating inputs have a tangent.
// For y = sqrt(x), the VJP is (dy * 0.5) / y, in the primal's float width.
void sqrtVjp(camel::core::VjpBuilder &builder, const camel::core::VjpCall &call) {
    using namespace camel::core;
    using namespace camel::core::rtdata;
    if (!camel::core::machineryTangentTypeOf(builder.nodeType(call.inputs[0]))) {
        return;
    }
    if (auto dy = builder.gradientOf(call.output)) {
        Type *type        = builder.nodeType(call.output);
        const bool narrow = type->code() == TypeCode::Float32;
        const auto half =
            builder.addStatic(narrow ? toSlot<Float32>(0.5f) : toSlot<Float64>(0.5), type);
        const std::array<vjp_node_t, 2> scaleInputs{*dy, half};
        const auto scaled = builder.addOper(type, narrow ? ":op/mul_f" : ":op/mul_d", scaleInputs);
        const std::array<vjp_node_t, 2> divideInputs{scaled, call.output};
        const auto dx = builder.addOper(type, narrow ? ":op/div_f" : ":op/div_d", divideInputs);
        builder.accumulateGradient(call.inputs[0], dx);
    }
}

// For y = erf(x), the VJP is dy * 2 / sqrt(pi) * exp(-x * x), in the primal's float width; erfc
// is its negation.
template <bool Complement>
void erfVjp(camel::core::VjpBuilder &builder, const camel::core::VjpCall &call) {
    using namespace camel::core;
    using namespace camel::core::rtdata;
    if (!camel::core::machineryTangentTypeOf(builder.nodeType(call.inputs[0]))) {
        return;
    }
    if (auto dy = builder.gradientOf(call.output)) {
        Type *type              = builder.nodeType(call.output);
        const bool narrow       = type->code() == TypeCode::Float32;
        const char *mul         = narrow ? ":op/mul_f" : ":op/mul_d";
        constexpr double kScale = 1.1283791670955126 * (Complement ? -1.0 : 1.0);
        const auto x            = call.inputs[0];
        const std::array<vjp_node_t, 2> squareInputs{x, x};
        const auto square = builder.addOper(type, mul, squareInputs);
        const std::array<vjp_node_t, 1> negInputs{square};
        const auto negated = builder.addOper(type, narrow ? ":op/neg_f" : ":op/neg_d", negInputs);
        const auto bell    = builder.addOper(
            type,
            narrow ? "math:exp_f" : "math:exp_d",
            std::array<vjp_node_t, 1>{negated});
        const auto scale = builder.addStatic(
            narrow ? toSlot<Float32>(static_cast<Float32>(kScale)) : toSlot<Float64>(kScale),
            type);
        const std::array<vjp_node_t, 2> scaled{*dy, scale};
        const auto weighted = builder.addOper(type, mul, scaled);
        const std::array<vjp_node_t, 2> product{weighted, bell};
        builder.accumulateGradient(call.inputs[0], builder.addOper(type, mul, product));
    }
}

// For y = tanh(x), the VJP is dy * (1 - y * y): the slope is one minus the output squared, the
// law the array width's `tanhVjp` writes (`stdlib/tensor/ops/elementwise_ops.cpp`), and the
// tangent multiplies it second, in that order.
void tanhVjp(camel::core::VjpBuilder &builder, const camel::core::VjpCall &call) {
    using namespace camel::core;
    using namespace camel::core::rtdata;
    if (!camel::core::machineryTangentTypeOf(builder.nodeType(call.inputs[0]))) {
        return;
    }
    if (auto dy = builder.gradientOf(call.output)) {
        Type *type         = builder.nodeType(call.output);
        const bool narrow  = type->code() == TypeCode::Float32;
        const char *mul    = narrow ? ":op/mul_f" : ":op/mul_d";
        const char *sub    = narrow ? ":op/sub_f" : ":op/sub_d";
        const vjp_node_t y = call.output;
        const std::array<vjp_node_t, 2> squareInputs{y, y};
        const auto square = builder.addOper(type, mul, squareInputs);
        const auto one =
            builder.addStatic(narrow ? toSlot<Float32>(1.0f) : toSlot<Float64>(1.0), type);
        const std::array<vjp_node_t, 2> slopeInputs{one, square};
        const auto slope = builder.addOper(type, sub, slopeInputs);
        const std::array<vjp_node_t, 2> product{*dy, slope};
        builder.accumulateGradient(call.inputs[0], builder.addOper(type, mul, product));
    }
}

// For y = exp(x), the VJP is dy * y: the value the operator already produced multiplies the
// tangent, the law the array width's `expVjp` writes.
void expScalarVjp(camel::core::VjpBuilder &builder, const camel::core::VjpCall &call) {
    using namespace camel::core;
    if (!camel::core::machineryTangentTypeOf(builder.nodeType(call.inputs[0]))) {
        return;
    }
    if (auto dy = builder.gradientOf(call.output)) {
        Type *type         = builder.nodeType(call.output);
        const bool narrow  = type->code() == TypeCode::Float32;
        const char *mul    = narrow ? ":op/mul_f" : ":op/mul_d";
        const vjp_node_t y = call.output;
        const std::array<vjp_node_t, 2> product{*dy, y};
        builder.accumulateGradient(call.inputs[0], builder.addOper(type, mul, product));
    }
}

} // namespace

MathModule::MathModule(context_ptr_t ctx) : BuiltinModule("math", ctx) {
    // Every signature is a declaration of `stdlib/math.cml`: the fixed ones and the ones whose
    // result type C++ computed before (`sqrt`, `shl`, `shr`, written out per spelling). The C++
    // side keeps the kernels, the executor, the operator traits, the numeric
    // semantics and the values.
    // The constants are values (design principles 10.1): `math::pi`, written without
    // parentheses, is the constant itself where it is used. (They were functions of no
    // arguments, `pi()`, while a module could export nothing but functions and types.)
    exportValue(Reference("pi"), std::make_shared<DoubleData>(std::numbers::pi));
    exportValue(Reference("e"), std::make_shared<DoubleData>(std::numbers::e));
    exportValue(
        Reference("inf"),
        std::make_shared<DoubleData>(std::numeric_limits<double>::infinity()));
    exportValue(
        Reference("nan"),
        std::make_shared<DoubleData>(std::numeric_limits<double>::quiet_NaN()));
}

module_ptr_t MathModule::create(context_ptr_t ctx) { return std::make_shared<MathModule>(ctx); }

bool MathModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("math", [ctx]() { return createMathExecutor(ctx); });
    // Every math operator is a function of its arguments.
    for (const auto &[name, op] : getMathOpsMap()) {
        camel::core::OperatorTable::instance().set(
            "math:" + name,
            {.pure = true, .elementwise = false});
    }
    // Their numeric semantics (design principles 7.1): after the traits, which the table checks
    // a declaration against.
    declareMathOperatorSemantics();
    camel::core::DerivativeRegistry::instance().setRule("math:sqrt", &sqrtVjp);
    camel::core::DerivativeRegistry::instance().setRule("math:sqrt_d", &sqrtVjp);
    for (const char *uri : {"math:erf_f", "math:erf_d"}) {
        camel::core::DerivativeRegistry::instance().setRule(uri, &erfVjp<false>);
    }
    for (const char *uri : {"math:erfc_f", "math:erfc_d"}) {
        camel::core::DerivativeRegistry::instance().setRule(uri, &erfVjp<true>);
    }
    // `math:abs_d` stays without a rule, and the reason is a missing capability rather than an
    // oversight: its law is a branch -- `dy` where `x >= 0`, `-dy` where it is not, which is the
    // array width's `absVjp` -- and the scalar graph has no operator to write that branch with.
    // `:op/ge_d` answers a bool and no conversion takes a bool to a float, so the sign cannot be
    // turned into a number; the quotient `x / abs(x)` is not a substitute, because at zero it
    // answers nothing where the array law answers `dy`, and two widths that disagree at the
    // boundary are worse than a gradient refused by name.
    for (const char *uri : {"math:tanh_d"}) {
        camel::core::DerivativeRegistry::instance().setRule(uri, &tanhVjp);
    }
    for (const char *uri : {"math:exp_d"}) {
        camel::core::DerivativeRegistry::instance().setRule(uri, &expScalarVjp);
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new MathModule(ctx->shared_from_this()); }
}
