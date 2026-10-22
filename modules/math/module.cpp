// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/data/primary.h"
#include "camel/core/derivative.h"
#include "camel/core/operator.h"
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
    if (!tangentTypeOf(builder.nodeType(call.inputs[0]))) {
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

} // namespace

MathModule::MathModule(context_ptr_t ctx) : BuiltinModule("math", ctx) {
    // Every signature is a declaration of `stdlib/math.cml`: the fixed ones and the ones whose
    // result type C++ computed before (`sqrt`, `shl`, `shr`, written out per spelling; handover
    // H202). The C++ side keeps the kernels, the executor, the operator traits, the numeric
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
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new MathModule(ctx->shared_from_this()); }
}
