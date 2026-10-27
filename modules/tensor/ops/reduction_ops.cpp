// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reduction and normalization operators.
 *
 * `sum(t)` / `mean(t)` reduce everything to the operand's element type. The axis forms
 * (`sum(t, axis)`, `sum(t, axis, keepdims)`, ...) share the export name with
 * the full forms and are selected by arity. Softmax-family operators take an
 * optional axis that defaults to the last one.
 *
 * Where the accumulation happens, per form (reduction and rounding are one rule,
 * design principles 7.3):
 *
 *   - `sum(t)`, `mean(t)`: accumulate exactly as the axis forms do — f64 for a floating
 *     element type (kernels/reduce.h `sumAll`), the exact int64 accumulator for an int64
 *     or bool one — and answer the element type, converting once at the end: the same
 *     element type and the same one rounding as `sum(t, axis, ...)`. A mean of an integer
 *     or bool tensor is f32, as by axis.
 *   - `sum(t, axis, ...)`, `mean(t, axis, ...)`: keep the element type (bf16, f16 and
 *     f64 keep theirs; a mean of integers or bools is f32) and accumulate in f64,
 *     rounding once when the element is stored (kernels/reduce.cpp).
 *   - `max` / `min` / `argmax` / `argmin` over an axis: no accumulator at all; they
 *     carry element values and IEEE 754-2019 maximum/minimum are exactly
 *     associative and commutative (7.7). Their rule is not this one: nothing is
 *     converted, so there is no rounding to do once, and their result is an element
 *     (max/min, over an axis) or a position (argmax/argmin, int64).
 *
 * `array::sum` in stdlib/array.cml accumulates in f64 and rounds once for the same
 * reason, so the array path and the axis form agree bit for bit.
 */

#include "../kernels/reduce.h"

#include "camel/core/error/runtime.h"
#include "camel/core/type/composite/array.h"
#include "catalog.h"
#include "support.h"
#include <format>
#include <array>

namespace camel::tensor::ops {

using namespace camel::core::type;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
namespace k = camel::tensor::kernels;

namespace {

bool keepDimsArg(ArgsView &norm, size_t index) {
    return hasArg(norm, index) && boolArg(norm, index);
}

template <k::ReduceOp Op> slot_t reduceAxisKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("reduce", [&] {
        return wrap(norm.resultType(), k::reduceAxis(
            Op,
            tensorArg(norm, 0),
            intArg(norm, 1),
            keepDimsArg(norm, 2),
            resultAllocator()));
    });
}

InferFn reduceAxisInfer(k::ReduceOp op) {
    return [op](const InferContext &ctx) -> std::optional<Type *> {
        const TensorFacts in          = ctx.facts(0);
        const bool keepDims           = ctx.has(2) ? ctx.constBool(2).value_or(false) : false;
        std::optional<TypeCode> dtype = in.dtype;
        if (op == k::ReduceOp::Mean) {
            // A mean of floats keeps the element type (f64 stays f64, bf16 and f16 stay
            // themselves and round once when the mean is stored); a mean of integers and bools
            // has always been f32.
            dtype = (dtype == TypeCode::Float64 || dtype == TypeCode::BFloat16 ||
                     dtype == TypeCode::Float16)
                        ? dtype
                        : std::optional(TypeCode::Float32);
        } else if (op == k::ReduceOp::ArgMax || op == k::ReduceOp::ArgMin) {
            dtype = TypeCode::Int64;
        } else if (op == k::ReduceOp::Sum && dtype == TypeCode::Bool) {
            dtype = TypeCode::Int64;
        }
        // Without a constant keepdims flag the rank is only known when it cannot change.
        if (ctx.has(2) && !ctx.constBool(2)) {
            return tensorOf(dtype, std::nullopt);
        }
        return tensorOf(dtype, reduceShape(in.shape, ctx.constInt(1), keepDims));
    };
}

template <k::ReduceOp Op> OpDef reduceAxisDef(std::string_view name, std::string_view exportName) {
    return OpDef{
        .name    = name,
        .exports = {exportName},
        .params =
            {{"t", ParamKind::Tensor},
             {"axis", ParamKind::Int},
             {"keepdims", ParamKind::Bool, true}},
        .resultDoc = "float[..]",
        .infer     = reduceAxisInfer(Op),
        .kernel        = &positionally<reduceAxisKernel<Op>>,
        .traits        = {},
        .withAfterNorm = true,
    };
}

/// The element type the whole-tensor `sum` and `mean` answer: the rule of their axis forms
/// (reduceAxisInfer), so the full form and the axis form agree on the element type the way they
/// agree on the accumulation. A float element type keeps itself, an int64 or bool sum is int64
/// (the exact accumulator), and a mean of an integer or bool element type is f32.
TypeCode wholeReductionDtype(k::ReduceOp op, std::optional<TypeCode> dtype) {
    if (!dtype) {
        // The operand states no element type (the bare `Tensor` spelling). Such a value reads as
        // `float` by the element-type rule (an unstated element type takes the array and literal
        // rule, f64), so the answer is `float` — the width this reduction answered with while its
        // result type was `float`, which is what a caller of an unstated element type reads.
        return TypeCode::Float64;
    }
    switch (*dtype) {
    case TypeCode::Bool:
        // A bool tensor is summed exactly in int64; a mean of bools has always been f32.
        return op == k::ReduceOp::Mean ? TypeCode::Float32 : TypeCode::Int64;
    case TypeCode::Int32:
    case TypeCode::Int64:
        // An integer sum keeps its element type and is the exact int64 accumulator truncated into
        // it, which is what the axis form stores; a mean of integers has always been f32.
        return op == k::ReduceOp::Mean ? TypeCode::Float32 : *dtype;
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::BFloat16:
    case TypeCode::Float16:
        // Every float element type keeps itself: a sum rounds into it once, and a mean keeps
        // f64, bf16 and f16 and answers f32 for f32 (the axis form's rule).
        return op == k::ReduceOp::Mean && *dtype == TypeCode::Float32 ? TypeCode::Float32 : *dtype;
    default:
        // No other element type is one this reduction answers for.
        return TypeCode::Float64;
    }
}

/// The accumulator converted into the result's element type: the one rounding of the
/// whole-tensor reduction (design principles 7.3), the same rounding the axis form stores with.
/// A bf16 or f16 result is the f32 the conversion gives brought to that type once.
slot_t elementTypedSlot(TypeCode result, double value) {
    switch (result) {
    case TypeCode::Float32:
        return camel::core::rtdata::toSlot(static_cast<float>(value));
    case TypeCode::BFloat16:
        return camel::core::rtdata::halfFromValue<camel::core::rtdata::BFloat16>(value);
    case TypeCode::Float16:
        return camel::core::rtdata::halfFromValue<camel::core::rtdata::Float16>(value);
    default:
        return camel::core::rtdata::toSlot(value);
    }
}

/// The element type the result slot is written as: the type the node itself carries, which is the
/// fact the infer and the kernel both read. A node that states no type, or states an array where
/// the whole-tensor reduction answers one element, is written as the f64 answer this operator
/// gave while its result type was `float`.
TypeCode resultElementCode(const Type *result) {
    if (result == nullptr || result->code() == TypeCode::Array) {
        return TypeCode::Float64;
    }
    return result->code();
}

slot_t sumAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    Array *t              = tensorArg(norm, 0);
    const TypeCode result = resultElementCode(norm.resultType());
    return runKernel("sum", [&] {
        // An integer result is the exact int64 accumulator (5.1, 7.7): int64 as it stands, and
        // int32 truncated into its own element type, where truncating once gives the same bits as
        // wrapping at every step — what the axis form stores. Every float element type takes the
        // f64 accumulator converted once.
        if (result == TypeCode::Int64) {
            return camel::core::rtdata::toSlot(k::sumAllInt64(t));
        }
        if (result == TypeCode::Int32) {
            return camel::core::rtdata::toSlot(static_cast<int32_t>(k::sumAllInt64(t)));
        }
        return elementTypedSlot(result, k::sumAll(t));
    });
}

slot_t meanAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    Array *t = tensorArg(norm, 0);
    // The definition of `mean` is sum(x) / (numel(x) as float) (stdlib/tensor_definitions.cml),
    // and the canonical form of the element type T is T(sum(float(x)) / float(numel(x))): the
    // sum and the division both happen in f64 and the result is rounded into T once, which is
    // what this kernel computes. For a tensor without elements that float division by zero
    // faults, so the kernel raises the same fault (design principles 7.1: a kernel agrees with
    // its definition, faults included). Raised outside runKernel, which would turn it into a
    // kernel error.
    if (t->numel() == 0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }
    const TypeCode result = resultElementCode(norm.resultType());
    return runKernel("mean", [&] {
        // The accumulation and the division are in f64, and the conversion into the element type
        // is the only rounding of the whole reduction — never a sum rounded to the element type
        // and then divided again.
        return elementTypedSlot(result, k::sumAll(t) / static_cast<double>(t->numel()));
    });
}

/// The whole-tensor form's result type, from the operand's element type.
InferFn wholeReductionInfer(k::ReduceOp op) {
    return [op](const InferContext &ctx) -> std::optional<Type *> {
        return Type::create(wholeReductionDtype(op, ctx.facts(0).dtype));
    };
}

int64_t axisOrLast(ArgsView &norm, size_t index) {
    return hasArg(norm, index) ? intArg(norm, index) : -1;
}

slot_t softmaxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax", [&] {
        return wrap(
            norm.resultType(),
            k::softmax(tensorArg(norm, 0), axisOrLast(norm, 1), resultAllocator()));
    });
}

slot_t logSoftmaxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("log_softmax", [&] {
        return wrap(
            norm.resultType(),
            k::logSoftmax(tensorArg(norm, 0), axisOrLast(norm, 1), resultAllocator()));
    });
}

slot_t softmaxGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax_grad", [&] {
        return wrap(norm.resultType(), k::softmaxGrad(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            axisOrLast(norm, 2),
            resultAllocator()));
    });
}

std::optional<Type *> floatLikeFirst(const InferContext &ctx) {
    const auto dtype = ctx.facts(0).dtype;
    return tensorOf(dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                               : std::optional(TypeCode::Float32),
                    ctx.facts(0).shape);
}

/// softmax(t, axis = -1), log_softmax: the axis must exist.
std::optional<Type *> softmaxInfer(const InferContext &ctx) {
    const auto shape = ctx.facts(0).shape;
    const auto dtype = ctx.facts(0).dtype;
    const auto axis  = ctx.has(1) ? ctx.constInt(1) : std::optional<int64_t>(-1);
    if (shape && axis) {
        const auto rank = static_cast<int64_t>(shape->size());
        if (*axis < -rank || *axis >= rank) {
            throw ShapeError(std::format(
                "axis {} is out of range for the rank-{} input {}",
                *axis,
                rank,
                formatShape(*shape)));
        }
    }
    return tensorOf(
        dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                   : std::optional(TypeCode::Float32),
        shape);
}

/// layer_norm(x, gamma, beta): gamma and beta cover the input's trailing dimensions.
std::optional<Type *> layerNormInfer(const InferContext &ctx) {
    const auto x = ctx.facts(0).shape;
    for (size_t i : {size_t{1}, size_t{2}}) {
        const auto p = ctx.facts(i).shape;
        if (!x || !p) {
            continue;
        }
        bool fits = p->size() <= x->size();
        for (size_t d = 0; fits && d < p->size(); ++d) {
            const std::optional<int64_t> want = (*x)[x->size() - p->size() + d], have = (*p)[d];
            fits = !want.has_value() || !have.has_value() || want == have;
        }
        if (!fits) {
            throw ShapeError(std::format(
                "{} {} does not match the trailing dimensions of the input {}",
                i == 1 ? "gamma" : "beta",
                formatShape(*p),
                formatShape(*x)));
        }
    }
    return tensorOf(
        x && ctx.facts(0).dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                                    : std::optional(TypeCode::Float32),
        x);
}

slot_t layerNormKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("layer_norm", [&] {
        const double eps = hasArg(norm, 3) ? numberArg(norm, 3) : 1e-5;
        return wrap(norm.resultType(), k::layerNorm(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            tensorArg(norm, 2),
            eps,
            resultAllocator()));
    });
}

// ---------------------------------------------------------------- VJP rules

void sumVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(
                b,
                call.inputs[0],
                "tensor:broadcast_like",
                {*dy, call.inputs[0]}));
    }
}

void meanVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t x = call.inputs[0];
        // dy is a scalar of the mean's element type (design principles 7.3): an f32 mean's
        // pullback shares the gradient out in f32, not in the f64 the count is written in. The
        // count is narrowed to dy's width and the division uses that width's operator, so an f32
        // mean does not divide an f32 gradient by an f64 count as if it were f64.
        const vjp_node_t count =
            addFloatOper(b, ":op/ltod", {b.addOper(Type::Int64(), "tensor:numel", std::array{x})});
        const vjp_node_t atWidth = floatGradientAtWidth(b, *dy, count);
        const type::Type *dyType = b.nodeType(*dy);
        const bool narrow = dyType != nullptr && dyType->code() == type::TypeCode::Float32;
        const vjp_node_t share =
            narrow ? b.addOper(
                         type::Type::Float32(), ":op/div_f", std::array<vjp_node_t, 2>{*dy, atWidth})
                   : addFloatOper(b, ":op/div_d", {*dy, atWidth});
        b.accumulateGradient(x, addTensorOperFor(b, x, "tensor:broadcast_like", {share, x}));
    }
}

/// dy of a reduction over an axis, re-expanded to the input's shape.
vjp_node_t expandReduced(VjpBuilder &b, const VjpCall &call, vjp_node_t dy) {
    const vjp_node_t keepDims = call.inputs.size() > 2 ? call.inputs[2] : staticBool(b, false);
    return addTensorOperFor(
        b,
        call.inputs[0],
        "tensor:expand_axis",
        {dy, call.inputs[0], call.inputs[1], keepDims});
}

void sumAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], expandReduced(b, call, *dy));
    }
}

void meanAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t extent = addFloatOper(
            b,
            ":op/ltod",
            {b.addOper(Type::Int64(), "tensor:dim", std::array{call.inputs[0], call.inputs[1]})});
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:divide", {expandReduced(b, call, *dy), extent}));
    }
}

// max / min over an axis: the gradient goes to the elements equal to the extremum. The selection
// is a bool mask and a mask is not a number (design principles 5.1): the conversion the
// multiplication needs is written, and `cast_like` puts the mask at the gradient's element type,
// which is what the rule can name - it serves f32 and f64 models alike.
void extremumAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t expanded = expandReduced(b, call, *dy);
        const vjp_node_t selected = addTensorOper(
            b, "tensor:strict_eq", {call.inputs[0], expandReduced(b, call, call.output)});
        const vjp_node_t share =
            addTensorOperFor(b, expanded, "tensor:cast_like", {selected, expanded});
        b.accumulateGradient(
            call.inputs[0], addTensorOper(b, "tensor:multiply", {share, expanded}));
    }
}

void softmaxVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 2);
    if (auto dy = b.gradientOf(call.output)) {
        std::vector<vjp_node_t> inputs{call.output, *dy};
        if (call.inputs.size() > 1) {
            inputs.push_back(call.inputs[1]);
        }
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:softmax_grad", inputs));
    }
}

// y = log_softmax(x): dx = dy - exp(y) * sum(dy, axis, keepdims).
void logSoftmaxVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t axis = call.inputs.size() > 1 ? call.inputs[1] : staticInt(b, -1);
        const vjp_node_t total =
            addTensorOper(b, "tensor:sum_axis", {*dy, axis, staticBool(b, true)});
        const vjp_node_t probs = addTensorOper(b, "tensor:exp", {call.output});
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(
                b,
                "tensor:subtract",
                {*dy, addTensorOper(b, "tensor:multiply", {probs, total})}));
    }
}

// layer_norm(x, gamma, beta, eps?)
void layerNormVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 4);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t x = call.inputs[0], gamma = call.inputs[1];
        for (int64_t which = 0; which < 3; ++which) {
            std::vector<vjp_node_t> inputs{x, gamma, *dy, staticInt(b, which)};
            if (call.inputs.size() > 3) {
                inputs.push_back(call.inputs[3]);
            }
            b.accumulateGradient(
                call.inputs[static_cast<size_t>(which)],
                addTensorOperFor(
                    b,
                    call.inputs[static_cast<size_t>(which)],
                    "tensor:layer_norm_grad",
                    inputs));
        }
    }
}

} // namespace

std::vector<OpDef> reductionOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "sum",
        .exports   = {"sum"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "the tensor's element type",
        .infer     = wholeReductionInfer(k::ReduceOp::Sum),
        .kernel    = &sumAllKernel,
        .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Sum>("sum_axis", "sum"));
    defs.push_back(OpDef{
        .name      = "mean",
        .exports   = {"mean"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "the tensor's element type (f32 for an integer or bool one)",
        .infer     = wholeReductionInfer(k::ReduceOp::Mean),
        .kernel    = &meanAllKernel,
        .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Mean>("mean_axis", "mean"));
    defs.push_back(reduceAxisDef<k::ReduceOp::Max>("max_axis", "max"));
    defs.push_back(reduceAxisDef<k::ReduceOp::Min>("min_axis", "min"));
    defs.push_back(reduceAxisDef<k::ReduceOp::ArgMax>("argmax_axis", "argmax"));
    defs.push_back(reduceAxisDef<k::ReduceOp::ArgMin>("argmin_axis", "argmin"));

    defs.push_back(OpDef{
        .name      = "softmax",
        .exports   = {"softmax"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "float[..]",
        .infer     = softmaxInfer,
        .kernel    = &softmaxKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "log_softmax",
        .exports   = {"log_softmax"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "float[..]",
        .infer     = softmaxInfer,
        .kernel    = &logSoftmaxKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "softmax_grad",
        .exports = {"softmax_grad"},
        .params =
            {{"y", ParamKind::Tensor}, {"dy", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "float[..]",
        .infer     = floatLikeFirst,
        .kernel    = &softmaxGradKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "layer_norm",
        .exports = {"layer_norm"},
        .params =
            {{"x", ParamKind::Tensor},
             {"gamma", ParamKind::Tensor},
             {"beta", ParamKind::Tensor},
             {"eps", ParamKind::Number, true}},
        .resultDoc = "float[..]",
        .infer     = layerNormInfer,
        .kernel    = &layerNormKernel,
        .traits    = {}});
    setVjp(defs, "sum", &sumVjp);
    setVjp(defs, "mean", &meanVjp);
    setVjp(defs, "sum_axis", &sumAxisVjp);
    setVjp(defs, "mean_axis", &meanAxisVjp);
    setVjp(defs, "max_axis", &extremumAxisVjp);
    setVjp(defs, "min_axis", &extremumAxisVjp);
    setVjp(defs, "argmax_axis", &camel::core::noGradient);
    setVjp(defs, "argmin_axis", &camel::core::noGradient);
    setVjp(defs, "softmax", &softmaxVjp);
    setVjp(defs, "log_softmax", &logSoftmaxVjp);
    setVjp(defs, "layer_norm", &layerNormVjp);
    return defs;
}

} // namespace camel::tensor::ops
