// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor creation operators. Shape arguments are int[] values; when their
 * contents are constant (export time) the result shape is known statically,
 * otherwise only the rank is unknown. Random operators and `seed` touch the
 * module RNG and are therefore not pure.
 */

#include "../interop.h"
#include "../kernels/elementwise.h"
#include "../kernels/layout.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

/// Result type of an operator whose shape is given by argument `index`.
std::optional<StaticShape> shapeFromArg(const InferContext &ctx, size_t index) {
    if (auto values = ctx.constInts(index)) {
        return StaticShape(values->begin(), values->end());
    }
    // A shape written in place with computed extents, `[n, 3]`: the rank and the constant
    // extents are known, a computed extent is not.
    if (auto partial = ctx.partialInts(index)) {
        StaticShape shape;
        for (const auto &extent : *partial) {
            shape.push_back(extent && *extent >= 0 ? *extent : kUnknownDim);
        }
        return shape;
    }
    return std::nullopt;
}

InferFn shapedInfer(std::optional<TypeCode> dtype) {
    return [dtype](const InferContext &ctx) -> std::optional<Type *> {
        return tensorOf(dtype, shapeFromArg(ctx, 0));
    };
}

slot_t newKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("new", [&] {
        return wrap(
            norm.resultType(),
            tensorFromArray(norm.slot(0), norm.type(0), resultAllocator()));
    });
}

std::optional<Type *> newInfer(const InferContext &ctx) { return tensorOf(ctx.facts(0)); }

slot_t zerosKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("zeros", [&] {
        return wrap(norm.resultType(),
            createTensor(TypeCode::Float32, shapeArg(norm, 0), resultAllocator(), true));
    });
}

slot_t onesKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("ones", [&] {
        return wrap(
            norm.resultType(),
            k::full(TypeCode::Float32, shapeArg(norm, 0), 1.0, resultAllocator()));
    });
}

slot_t fullKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("full", [&] {
        return wrap(norm.resultType(), k::full(
            norm.type(1)->code(),
            shapeArg(norm, 0),
            numberArg(norm, 1),
            resultAllocator()));
    });
}

std::optional<Type *> fullInfer(const InferContext &ctx) {
    return tensorOf(ctx.type(1)->code(), shapeFromArg(ctx, 0));
}

slot_t randomKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("random", [&] {
        return wrap(norm.resultType(), k::randomUniform(
            shapeArg(norm, 0),
            numberArg(norm, 1),
            numberArg(norm, 2),
            resultAllocator()));
    });
}

slot_t randnKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("randn", [&] {
        const double mean   = hasArg(norm, 1) ? numberArg(norm, 1) : 0.0;
        const double stddev = hasArg(norm, 2) ? numberArg(norm, 2) : 1.0;
        return wrap(
            norm.resultType(),
            k::randomNormal(shapeArg(norm, 0), mean, stddev, resultAllocator()));
    });
}

slot_t rangeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("range", [&] {
        int64_t start = 0, stop = 0, step = 1;
        if (norm.size() == 1) {
            stop = intArg(norm, 0);
        } else {
            start = intArg(norm, 0);
            stop  = intArg(norm, 1);
            if (norm.size() == 3) {
                step = intArg(norm, 2);
            }
        }
        return wrap(norm.resultType(), k::arange(start, stop, step, resultAllocator()));
    });
}

std::optional<Type *> rangeInfer(const InferContext &) {
    return tensorOf(TypeCode::Int64, StaticShape{kUnknownDim});
}

slot_t eyeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(
        "eye",
        [&] { return wrap(norm.resultType(), k::eye(intArg(norm, 0), resultAllocator())); });
}

std::optional<Type *> eyeInfer(const InferContext &ctx) {
    const int64_t n = ctx.constInt(0).value_or(kUnknownDim);
    return tensorOf(TypeCode::Float32, StaticShape{n, n});
}

slot_t seedKernel(ArgsView &, ArgsView &norm, context::Context &) {
    k::seedRandom(static_cast<uint64_t>(intArg(norm, 0)));
    return NullSlot;
}

const std::vector<ParamSpec> kShapeParam = {{"shape", ParamKind::IntArray}};

// new(values): an array's elements in a tensor of the inferred shape. A tensor argument is that
// tensor in a new value, so its gradient passes through; an array argument is continuous when
// its elements are (design principles 9.9), and its adjoint would put the elements of dy back in
// an array of the same shape. No operator of this module produces an array from a tensor's
// gradient, so an array that depends on a differentiated parameter is refused by name instead of
// silently dropped — the refusal `reshape` and `concat` make for the same reason. A constant
// array starts no gradient flow, so the call that converts one is never visited.
void newVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (isTensorNode(b, call.inputs[0])) {
        if (auto dy = b.gradientOf(call.output)) {
            b.accumulateGradient(call.inputs[0], *dy);
        }
        return;
    }
    if (b.isActive(call.inputs[0])) {
        refuseVjp(
            call,
            "its input is a numeric array of type '" + b.nodeType(call.inputs[0])->toString() +
                "', not a tensor, and this module has no operator that carries a tensor's "
                "gradient back into an array; the array's gradient would be dropped silently. "
                "Build the tensor from a differentiable tensor (a parameter, a slice of one) "
                "instead of converting an array");
    }
}

} // namespace

std::vector<OpDef> creationOps() {
    const OpTraits impure{.pure = false};
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "new",
        .exports   = {"new"},
        .params    = {{"values", ParamKind::TensorLike}},
        .resultDoc = "Tensor",
        .infer     = newInfer,
        .kernel    = &newKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "empty",
        .exports   = {"empty"},
        .params    = kShapeParam,
        .resultDoc = "Tensor",
        .infer     = shapedInfer(TypeCode::Float32),
        .kernel    = &zerosKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "zeros",
        .exports   = {"zeros"},
        .params    = kShapeParam,
        .resultDoc = "Tensor",
        .infer     = shapedInfer(TypeCode::Float32),
        .kernel    = &zerosKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "ones",
        .exports   = {"ones"},
        .params    = kShapeParam,
        .resultDoc = "Tensor",
        .infer     = shapedInfer(TypeCode::Float32),
        .kernel    = &onesKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "full",
        .exports   = {"full"},
        .params    = {{"shape", ParamKind::IntArray}, {"value", ParamKind::Number}},
        .resultDoc = "Tensor",
        .infer     = fullInfer,
        .kernel    = &fullKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "random",
        .exports = {"random"},
        .params =
            {{"shape", ParamKind::IntArray},
             {"low", ParamKind::Number},
             {"high", ParamKind::Number}},
        .resultDoc = "Tensor",
        .infer     = shapedInfer(TypeCode::Float32),
        .kernel    = &randomKernel,
        .traits    = impure});
    defs.push_back(OpDef{
        .name    = "randn",
        .exports = {"randn"},
        .params =
            {{"shape", ParamKind::IntArray},
             {"mean", ParamKind::Number, true},
             {"std", ParamKind::Number, true}},
        .resultDoc = "Tensor",
        .infer     = shapedInfer(TypeCode::Float32),
        .kernel    = &randnKernel,
        .traits    = impure});
    defs.push_back(OpDef{
        .name    = "range",
        .exports = {"range"},
        .params =
            {{"start", ParamKind::Int},
             {"stop", ParamKind::Int, true},
             {"step", ParamKind::Int, true}},
        .resultDoc = "Tensor<int64>",
        .infer     = rangeInfer,
        .kernel    = &rangeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "eye",
        .exports   = {"eye"},
        .params    = {{"n", ParamKind::Int}},
        .resultDoc = "Tensor",
        .infer     = eyeInfer,
        .kernel    = &eyeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "seed",
        .exports   = {"seed"},
        .params    = {{"seed", ParamKind::Int}},
        .resultDoc = "void",
        .infer     = [](const InferContext &) -> std::optional<Type *> { return Type::Void(); },
        .kernel    = &seedKernel,
        .traits    = impure});
    setVjp(defs, "new", &newVjp);
    return defs;
}

} // namespace camel::tensor::ops
