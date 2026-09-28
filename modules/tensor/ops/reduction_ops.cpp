/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Reduction and normalization operators.
 *
 * `sum(t)` / `mean(t)` reduce everything to a Camel float. The axis forms
 * (`sum(t, axis)`, `sum(t, axis, keepdims)`, ...) share the export name with
 * the full forms and are selected by arity. Softmax-family operators take an
 * optional axis that defaults to the last one.
 */

#include "../kernels/reduce.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

bool keepDimsArg(ArgsView &norm, size_t index) {
    return hasArg(norm, index) && boolArg(norm, index);
}

template <k::ReduceOp Op> slot_t reduceAxisKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("reduce", [&] {
        return wrap(k::reduceAxis(
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
            dtype = TypeCode::Float32;
        } else if (op == k::ReduceOp::ArgMax) {
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
        .resultDoc = "Tensor",
        .infer     = reduceAxisInfer(Op),
        .kernel    = &reduceAxisKernel<Op>,
        .traits    = {},
    };
}

slot_t sumAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("sum", [&] {
        return camel::core::rtdata::toSlot(k::sumAll(tensorArg(norm, 0)));
    });
}

slot_t meanAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("mean", [&] {
        TensorObject *t = tensorArg(norm, 0);
        if (t->numel() == 0) {
            throw std::invalid_argument("mean of an empty tensor");
        }
        return camel::core::rtdata::toSlot(k::sumAll(t) / static_cast<double>(t->numel()));
    });
}

std::optional<Type *> scalarFloatInfer(const InferContext &) { return Type::Float64(); }

int64_t axisOrLast(ArgsView &norm, size_t index) {
    return hasArg(norm, index) ? intArg(norm, index) : -1;
}

slot_t softmaxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax", [&] {
        return wrap(k::softmax(tensorArg(norm, 0), axisOrLast(norm, 1), resultAllocator()));
    });
}

slot_t logSoftmaxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("log_softmax", [&] {
        return wrap(k::logSoftmax(tensorArg(norm, 0), axisOrLast(norm, 1), resultAllocator()));
    });
}

slot_t softmaxGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax_grad", [&] {
        return wrap(k::softmaxGrad(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            axisOrLast(norm, 2),
            resultAllocator()));
    });
}

std::optional<Type *> floatLikeFirst(const InferContext &ctx) {
    return tensorOf(TypeCode::Float32, ctx.facts(0).shape);
}

slot_t layerNormKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("layer_norm", [&] {
        const double eps = hasArg(norm, 3) ? numberArg(norm, 3) : 1e-5;
        return wrap(k::layerNorm(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            tensorArg(norm, 2),
            eps,
            resultAllocator()));
    });
}

} // namespace

std::vector<OpDef> reductionOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "sum",
        .exports   = {"sum"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "float",
        .infer     = scalarFloatInfer,
        .kernel    = &sumAllKernel,
        .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Sum>("sum_axis", "sum"));
    defs.push_back(OpDef{
        .name      = "mean",
        .exports   = {"mean"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "float",
        .infer     = scalarFloatInfer,
        .kernel    = &meanAllKernel,
        .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Mean>("mean_axis", "mean"));
    defs.push_back(reduceAxisDef<k::ReduceOp::Max>("max_axis", "max"));
    defs.push_back(reduceAxisDef<k::ReduceOp::Min>("min_axis", "min"));
    defs.push_back(reduceAxisDef<k::ReduceOp::ArgMax>("argmax_axis", "argmax"));

    defs.push_back(OpDef{
        .name      = "softmax",
        .exports   = {"softmax"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "Tensor",
        .infer     = floatLikeFirst,
        .kernel    = &softmaxKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "log_softmax",
        .exports   = {"log_softmax"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "Tensor",
        .infer     = floatLikeFirst,
        .kernel    = &logSoftmaxKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "softmax_grad",
        .exports = {"softmax_grad"},
        .params =
            {{"y", ParamKind::Tensor}, {"dy", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "Tensor",
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
        .resultDoc = "Tensor",
        .infer     = floatLikeFirst,
        .kernel    = &layerNormKernel,
        .traits    = {}});
    return defs;
}

} // namespace camel::tensor::ops
