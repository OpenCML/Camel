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
 * Updated: Oct. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * nn tensor operators as OpDefs in the shared operator registry: 2-D
 * convolution and its gradients, pooling and its gradients, inference-mode
 * batch normalization, embedding, and the fused softmax cross-entropy loss.
 *
 * Spatial arguments (stride, padding, pooling window) are optional trailing
 * ints; they default to stride 1, no padding, and a pooling stride equal to
 * the kernel size.
 */

#include "layers.h"

#include "../tensor/kernels/conv.h"
#include "../tensor/ops/registry.h"
#include "../tensor/ops/support.h"

#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"

#include <mutex>

namespace camel::nn {

namespace {

using namespace camel::core::type;
using namespace camel::tensor::ops;
namespace k      = camel::tensor::kernels;
using TensorType = camel::tensor::TensorType;
using camel::tensor::kUnknownDim;
using camel::tensor::StaticShape;
using camel::tensor::TensorObject;

int64_t optInt(ArgsView &norm, size_t index, int64_t fallback) {
    return hasArg(norm, index) ? intArg(norm, index) : fallback;
}

std::optional<int64_t> optConstInt(const InferContext &ctx, size_t index, int64_t fallback) {
    return ctx.has(index) ? ctx.constInt(index) : std::optional<int64_t>(fallback);
}

bool known(const std::optional<StaticShape> &shape) {
    if (!shape) {
        return false;
    }
    for (int64_t extent : *shape) {
        if (extent == kUnknownDim) {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------ conv2d

slot_t conv2dKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("conv2d", [&] {
        return wrap(
            k::conv2d(
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                tensorArg(norm, 2),
                optInt(norm, 3, 1),
                optInt(norm, 4, 0),
                resultAllocator()));
    });
}

std::optional<Type *> conv2dInfer(const InferContext &ctx) {
    const TensorFacts x = ctx.facts(0), w = ctx.facts(1);
    const auto stride = optConstInt(ctx, 3, 1), pad = optConstInt(ctx, 4, 0);
    if ((x.shape && x.shape->size() != 4) || (w.shape && w.shape->size() != 4)) {
        return std::nullopt;
    }
    if (known(x.shape) && known(w.shape) && stride && pad) {
        return tensorOf(TypeCode::Float32, k::conv2dShape(*x.shape, *w.shape, *stride, *pad));
    }
    StaticShape out(4, kUnknownDim);
    if (x.shape) {
        out[0] = (*x.shape)[0];
    }
    if (w.shape) {
        out[1] = (*w.shape)[0];
    }
    return tensorOf(TypeCode::Float32, out);
}

slot_t conv2dInputGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("conv2d_input_grad", [&] {
        return wrap(
            k::conv2dInputGrad(
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                tensorArg(norm, 2),
                optInt(norm, 3, 1),
                optInt(norm, 4, 0),
                resultAllocator()));
    });
}

slot_t conv2dKernelGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("conv2d_kernel_grad", [&] {
        return wrap(
            k::conv2dKernelGrad(
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                tensorArg(norm, 2),
                optInt(norm, 3, 1),
                optInt(norm, 4, 0),
                resultAllocator()));
    });
}

slot_t conv2dBiasGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("conv2d_bias_grad", [&] {
        return wrap(k::conv2dBiasGrad(tensorArg(norm, 1), resultAllocator()));
    });
}

/// Gradient operators return a tensor shaped like argument `index`.
InferFn shapedLike(size_t index) {
    return [index](const InferContext &ctx) -> std::optional<Type *> {
        return tensorOf(TypeCode::Float32, ctx.facts(index).shape);
    };
}

// ------------------------------------------------------------------ pooling

k::Window2d windowArg(ArgsView &norm, size_t first) {
    const int64_t kernel = intArg(norm, first);
    const int64_t stride = optInt(norm, first + 1, kernel);
    const int64_t pad    = optInt(norm, first + 2, 0);
    return k::Window2d{kernel, kernel, stride, stride, pad, pad};
}

template <k::PoolKind Kind> slot_t poolKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(Kind == k::PoolKind::Max ? "max_pool2d" : "avg_pool2d", [&] {
        return wrap(k::pool2d(Kind, tensorArg(norm, 0), windowArg(norm, 1), resultAllocator()));
    });
}

template <k::PoolKind Kind> slot_t poolGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(Kind == k::PoolKind::Max ? "max_pool2d_grad" : "avg_pool2d_grad", [&] {
        return wrap(
            k::pool2dGrad(
                Kind,
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                windowArg(norm, 2),
                resultAllocator()));
    });
}

std::optional<Type *> poolInfer(const InferContext &ctx) {
    const TensorFacts x = ctx.facts(0);
    if (x.shape && x.shape->size() != 4) {
        return std::nullopt;
    }
    const auto kernel = ctx.constInt(1);
    const auto stride = ctx.has(2) ? ctx.constInt(2) : kernel;
    const auto pad    = optConstInt(ctx, 3, 0);
    if (known(x.shape) && kernel && stride && pad) {
        const k::Window2d window{*kernel, *kernel, *stride, *stride, *pad, *pad};
        return tensorOf(TypeCode::Float32, k::pool2dShape(*x.shape, window));
    }
    StaticShape out(4, kUnknownDim);
    if (x.shape) {
        out[0] = (*x.shape)[0];
        out[1] = (*x.shape)[1];
    }
    return tensorOf(TypeCode::Float32, out);
}

// ------------------------------------------------------------------ batch_norm

slot_t batchNormKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("batch_norm", [&] {
        const double eps = hasArg(norm, 5) ? numberArg(norm, 5) : 1e-5;
        return wrap(
            k::batchNorm(
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                tensorArg(norm, 2),
                tensorArg(norm, 3),
                tensorArg(norm, 4),
                eps,
                resultAllocator()));
    });
}

/// A (y, running_mean, running_var)-shaped triple holding `tensors`.
slot_t tripleOf(TensorObject *first, TensorObject *second, TensorObject *third) {
    namespace mm          = camel::core::mm;
    const auto tensorType = [](const TensorObject *t) -> Type * {
        StaticShape shape(t->shapeSpan().begin(), t->shapeSpan().end());
        return TensorType::get(t->dtype(), std::move(shape));
    };
    TupleType *type = TupleType::create({tensorType(first), tensorType(second), tensorType(third)});
    auto *tuple     = ::Tuple::create(3, mm::autoSpace());
    tuple->set<slot_t>(0, camel::core::rtdata::toSlot<TensorObject *>(first), type);
    tuple->set<slot_t>(1, camel::core::rtdata::toSlot<TensorObject *>(second), type);
    tuple->set<slot_t>(2, camel::core::rtdata::toSlot<TensorObject *>(third), type);
    return camel::core::rtdata::toSlot<::Tuple *>(tuple);
}

slot_t batchNormTrainKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("batch_norm_train", [&] {
        const double momentum = hasArg(norm, 5) ? numberArg(norm, 5) : 0.1;
        const double eps      = hasArg(norm, 6) ? numberArg(norm, 6) : 1e-5;
        TensorObject *y, *mean, *var;
        k::batchNormTrain(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            tensorArg(norm, 2),
            tensorArg(norm, 3),
            tensorArg(norm, 4),
            momentum,
            eps,
            &y,
            &mean,
            &var,
            resultAllocator());
        return tripleOf(y, mean, var);
    });
}

/// batch_norm_train(x, ...): (y like x, running stats like running_mean/running_var).
std::optional<Type *> batchNormTrainInfer(const InferContext &ctx) {
    const TensorFacts x = ctx.facts(0);
    if (x.shape && x.shape->size() < 2) {
        return std::nullopt;
    }
    return TupleType::create(
        std::vector<Type *>{
            tensorOf(TypeCode::Float32, x.shape),
            tensorOf(TypeCode::Float32, ctx.facts(3).shape),
            tensorOf(TypeCode::Float32, ctx.facts(4).shape)});
}

slot_t batchNormTrainGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("batch_norm_train_grad", [&] {
        const double eps = hasArg(norm, 3) ? numberArg(norm, 3) : 1e-5;
        TensorObject *dx, *dgamma, *dbeta;
        k::batchNormTrainGrad(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            tensorArg(norm, 2),
            eps,
            &dx,
            &dgamma,
            &dbeta,
            resultAllocator());
        return tripleOf(dx, dgamma, dbeta);
    });
}

/// batch_norm_train_grad(x, gamma, dy): (dx like x, dgamma/dbeta like gamma).
std::optional<Type *> batchNormTrainGradInfer(const InferContext &ctx) {
    return TupleType::create(
        std::vector<Type *>{
            tensorOf(TypeCode::Float32, ctx.facts(0).shape),
            tensorOf(TypeCode::Float32, ctx.facts(1).shape),
            tensorOf(TypeCode::Float32, ctx.facts(1).shape)});
}

// ------------------------------------------------------------------ embedding / loss

slot_t embeddingKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("embedding", [&] {
        return wrap(embedding(tensorArg(norm, 0), tensorArg(norm, 1), resultAllocator()));
    });
}

std::optional<Type *> embeddingInfer(const InferContext &ctx) {
    const TensorFacts table = ctx.facts(0), idx = ctx.facts(1);
    StaticShape out{kUnknownDim, kUnknownDim};
    if (idx.shape && idx.shape->size() == 1) {
        out[0] = (*idx.shape)[0];
    }
    if (table.shape && table.shape->size() == 2) {
        out[1] = (*table.shape)[1];
    }
    return tensorOf(table.dtype, out);
}

slot_t embeddingTableGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("embedding_table_grad", [&] {
        return wrap(embeddingTableGrad(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            tensorArg(norm, 2),
            resultAllocator()));
    });
}

slot_t softmaxCrossEntropyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax_cross_entropy", [&] {
        return camel::core::rtdata::toSlot(
            softmaxCrossEntropy(tensorArg(norm, 0), tensorArg(norm, 1)));
    });
}

slot_t softmaxCrossEntropyGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax_cross_entropy_grad", [&] {
        return wrap(softmaxCrossEntropyGradLogits(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            numberArg(norm, 2),
            resultAllocator()));
    });
}

const OpTraits kPure{};

std::vector<ParamSpec> convParams(std::string_view third) {
    return {
        {"input", ParamKind::Tensor},
        {"kernel", ParamKind::Tensor},
        {third, ParamKind::Tensor},
        {"stride", ParamKind::Int, true},
        {"padding", ParamKind::Int, true},
    };
}

std::vector<ParamSpec> poolParams(bool withDy) {
    std::vector<ParamSpec> params{{"input", ParamKind::Tensor}};
    if (withDy) {
        params.push_back({"dy", ParamKind::Tensor});
    }
    params.push_back({"kernel", ParamKind::Int});
    params.push_back({"stride", ParamKind::Int, true});
    params.push_back({"padding", ParamKind::Int, true});
    return params;
}

// ---------------------------------------------------------------- VJP rules

void softmaxCrossEntropyVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(
                b,
                "nn:softmax_cross_entropy_grad",
                {call.inputs[0], call.inputs[1], *dy}));
    }
}

void embeddingVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "nn:embedding_table_grad", {call.inputs[0], call.inputs[1], *dy}));
    }
}

/// conv2d(input, kernel, bias, stride?, padding?): stride and padding are forwarded unchanged to
/// the gradient operators.
void conv2dVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 5);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::vector<vjp_node_t> gradInputs{call.inputs[0], call.inputs[1], *dy};
    gradInputs.insert(gradInputs.end(), call.inputs.begin() + 3, call.inputs.end());
    b.accumulateGradient(
        call.inputs[0],
        b.addOper(vjpTensorType(), "nn:conv2d_input_grad", gradInputs));
    b.accumulateGradient(
        call.inputs[1],
        b.addOper(vjpTensorType(), "nn:conv2d_kernel_grad", gradInputs));
    b.accumulateGradient(
        call.inputs[2],
        addTensorOper(b, "nn:conv2d_bias_grad", {call.inputs[2], *dy}));
}

/// batch_norm_train(x, gamma, beta, running_mean, running_var, momentum?, eps?)
///   -> (y, new_running_mean, new_running_var)
/// Only y carries gradient: the running-statistics blend is a function of the batch and the old
/// statistics (framework semantics: not differentiated), so the rule reads the gradient of
/// result element 0 only. eps (not momentum) is forwarded to the gradient operator; the running
/// statistics take no gradient.
void batchNormTrainVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 5, 7);
    auto dyAll = b.gradientOf(call.output);
    if (!dyAll) {
        return;
    }
    const vjp_node_t dy = b.addAccess(*dyAll, 0);
    std::vector<vjp_node_t> gradInputs{call.inputs[0], call.inputs[1], dy};
    if (call.inputs.size() > 6) {
        gradInputs.push_back(call.inputs[6]);
    }
    Type *gradType         = TupleType::create({vjpTensorType(), vjpTensorType(), vjpTensorType()});
    const vjp_node_t grads = b.addOper(gradType, "nn:batch_norm_train_grad", gradInputs);
    b.accumulateGradient(call.inputs[0], b.addAccess(grads, 0));
    b.accumulateGradient(call.inputs[1], b.addAccess(grads, 1));
    b.accumulateGradient(call.inputs[2], b.addAccess(grads, 2));
}

constexpr char kMaxPoolGradUri[] = "nn:max_pool2d_grad";
constexpr char kAvgPoolGradUri[] = "nn:avg_pool2d_grad";

/// pool2d(input, kernel, stride?, padding?) -> pool2d_grad(input, dy, kernel, stride?, padding?)
template <const char *GradUri> void pool2dVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 4);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::vector<vjp_node_t> gradInputs{call.inputs[0], *dy};
    gradInputs.insert(gradInputs.end(), call.inputs.begin() + 1, call.inputs.end());
    b.accumulateGradient(call.inputs[0], b.addOper(vjpTensorType(), GradUri, gradInputs));
}

std::vector<OpDef> nnTensorOps() {
    std::vector<OpDef> defs;
    defs.push_back(
        OpDef{
            .name      = "conv2d",
            .exports   = {"conv2d"},
            .params    = convParams("bias"),
            .resultDoc = "Tensor",
            .infer     = conv2dInfer,
            .kernel    = &conv2dKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "conv2d_input_grad",
            .exports   = {"conv2d_input_grad"},
            .params    = convParams("dy"),
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &conv2dInputGradKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "conv2d_kernel_grad",
            .exports   = {"conv2d_kernel_grad"},
            .params    = convParams("dy"),
            .resultDoc = "Tensor",
            .infer     = shapedLike(1),
            .kernel    = &conv2dKernelGradKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "conv2d_bias_grad",
            .exports   = {"conv2d_bias_grad"},
            .params    = {{"bias", ParamKind::Tensor}, {"dy", ParamKind::Tensor}},
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &conv2dBiasGradKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "max_pool2d",
            .exports   = {"max_pool2d"},
            .params    = poolParams(false),
            .resultDoc = "Tensor",
            .infer     = poolInfer,
            .kernel    = &poolKernel<k::PoolKind::Max>,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "avg_pool2d",
            .exports   = {"avg_pool2d"},
            .params    = poolParams(false),
            .resultDoc = "Tensor",
            .infer     = poolInfer,
            .kernel    = &poolKernel<k::PoolKind::Average>,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "max_pool2d_grad",
            .exports   = {"max_pool2d_grad"},
            .params    = poolParams(true),
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &poolGradKernel<k::PoolKind::Max>,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "avg_pool2d_grad",
            .exports   = {"avg_pool2d_grad"},
            .params    = poolParams(true),
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &poolGradKernel<k::PoolKind::Average>,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name    = "batch_norm",
            .exports = {"batch_norm"},
            .params =
                {{"input", ParamKind::Tensor},
                 {"mean", ParamKind::Tensor},
                 {"var", ParamKind::Tensor},
                 {"gamma", ParamKind::Tensor},
                 {"beta", ParamKind::Tensor},
                 {"eps", ParamKind::Number, true}},
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &batchNormKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name    = "batch_norm_train",
            .exports = {"batch_norm_train"},
            .params =
                {{"input", ParamKind::Tensor},
                 {"gamma", ParamKind::Tensor},
                 {"beta", ParamKind::Tensor},
                 {"running_mean", ParamKind::Tensor},
                 {"running_var", ParamKind::Tensor},
                 {"momentum", ParamKind::Number, true},
                 {"eps", ParamKind::Number, true}},
            .resultDoc = "(Tensor, Tensor, Tensor)",
            .infer     = batchNormTrainInfer,
            .kernel    = &batchNormTrainKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name    = "batch_norm_train_grad",
            .exports = {"batch_norm_train_grad"},
            .params =
                {{"input", ParamKind::Tensor},
                 {"gamma", ParamKind::Tensor},
                 {"dy", ParamKind::Tensor},
                 {"eps", ParamKind::Number, true}},
            .resultDoc = "(Tensor, Tensor, Tensor)",
            .infer     = batchNormTrainGradInfer,
            .kernel    = &batchNormTrainGradKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "embedding",
            .exports   = {"embedding"},
            .params    = {{"table", ParamKind::Tensor}, {"indices", ParamKind::Tensor}},
            .resultDoc = "Tensor",
            .infer     = embeddingInfer,
            .kernel    = &embeddingKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name    = "embedding_table_grad",
            .exports = {"embedding_table_grad"},
            .params =
                {{"table", ParamKind::Tensor},
                 {"indices", ParamKind::Tensor},
                 {"dy", ParamKind::Tensor}},
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &embeddingTableGradKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "softmax_cross_entropy",
            .exports   = {"softmax_cross_entropy"},
            .params    = {{"logits", ParamKind::Tensor}, {"target", ParamKind::Tensor}},
            .resultDoc = "float",
            .infer  = [](const InferContext &) -> std::optional<Type *> { return Type::Float64(); },
            .kernel = &softmaxCrossEntropyKernel,
            .traits = kPure});
    defs.push_back(
        OpDef{
            .name    = "softmax_cross_entropy_grad",
            .exports = {"softmax_cross_entropy_grad"},
            .params =
                {{"logits", ParamKind::Tensor},
                 {"target", ParamKind::Tensor},
                 {"dy", ParamKind::Number}},
            .resultDoc = "Tensor",
            .infer     = shapedLike(0),
            .kernel    = &softmaxCrossEntropyGradKernel,
            .traits    = kPure});
    setVjp(defs, "conv2d", &conv2dVjp);
    setVjp(defs, "batch_norm_train", &batchNormTrainVjp);
    setVjp(defs, "max_pool2d", &pool2dVjp<kMaxPoolGradUri>);
    setVjp(defs, "avg_pool2d", &pool2dVjp<kAvgPoolGradUri>);
    setVjp(defs, "embedding", &embeddingVjp);
    setVjp(defs, "softmax_cross_entropy", &softmaxCrossEntropyVjp);
    return defs;
}

} // namespace

void registerNnTensorOps() {
    static std::once_flag once;
    std::call_once(once, [] { OpRegistry::instance().add("nn", nnTensorOps()); });
}

} // namespace camel::nn
