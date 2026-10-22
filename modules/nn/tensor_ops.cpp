// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "../tensor/interop.h"
#include "../tensor/ops/indexing.h"
#include "../tensor/ops/registry.h"
#include "../tensor/ops/support.h"

#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/error/runtime.h"

#include <mutex>
#include <span>
#include <vector>

namespace camel::nn {

namespace {

using namespace camel::core::type;
using namespace camel::tensor::ops;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
namespace k      = camel::tensor::kernels;
using TensorType = camel::tensor::TensorType;
using camel::tensor::kUnknownDim;
using camel::tensor::StaticShape;
using camel::tensor::Array;

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

bool isIntegerCode(const std::optional<TypeCode> &code) {
    return !code || *code == TypeCode::Int32 || *code == TypeCode::Int64;
}

/// Class labels: an int array or a rank-1 integer tensor, read as int64.
std::vector<int64_t> labelArg(ArgsView &args, size_t index, std::string_view op) {
    Type *type = args.type(index);
    if (type->code() == TypeCode::Array) {
        const Type *elem = static_cast<const ArrayType *>(type)->elemType();
        if (elem == nullptr ||
            (elem->code() != TypeCode::Int32 && elem->code() != TypeCode::Int64)) {
            reportShapeError(op, "expects an integer label array");
        }
        return tensor::parseIntArray(args.get<::Array *>(index), type);
    }
    const Array *labels = tensorArg(args, index);
    if (labels->rank() != 1) {
        reportShapeError(op, "expects a rank-1 integer label tensor");
    }
    if (labels->dtype() != TypeCode::Int64 && labels->dtype() != TypeCode::Int32) {
        reportShapeError(op, "expects integer class labels");
    }
    std::vector<int64_t> values(labels->numel());
    for (uint64_t i = 0; i < labels->numel(); ++i) {
        values[static_cast<size_t>(i)] = labels->getAsInt64(i);
    }
    return values;
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

/**
 * The index semantics of max_pool2d and avg_pool2d (docs/design/op-semantics 5.2, example 4:
 * the window): x: [N, C, H, W] (f32), a square window of `kernel`, `stride` (default the
 * kernel) and `pad` (default 0):
 *
 *   params   oh = derived((H + 2*pad - kernel) floordiv stride + 1), ow likewise
 *   domain   d0 < N, d1 < C, d2 < oh, d3 < ow parallel; d4 < kernel, d5 < kernel reduction
 *   norm 0   (d0, d1, stride*d2 + d4 - pad, stride*d3 + d5 - pad)
 *            valid inside [0, H) x [0, W); outside: max excludes the element (padding with 0
 *            would change a negative window's maximum), avg reads +0.0 (count_include_pad)
 *   write    (d0, d1, d2, d3) combine    max: IEEE maximum from -inf; avg: f64 sum from +0
 *                                        divided by kernel*kernel, rounded once to f32
 */
IndexFn poolIndexing(bool max) {
    namespace sem = camel::core::sem;
    using Rel     = sem::Constraint::Rel;
    using ExprId  = sem::ExprId;
    return [max](const InferContext &ctx) -> std::optional<sem::IndexedInstance> {
        const TensorFacts x = ctx.facts(0);
        const auto kernel   = ctx.constInt(1);
        const auto stride   = ctx.has(2) ? ctx.constInt(2) : kernel;
        const auto pad      = optConstInt(ctx, 3, 0);
        if (!x.shape || x.shape->size() != 4 || x.dtype != TypeCode::Float32 || !kernel ||
            !stride || !pad || *kernel <= 0 || *stride <= 0 || *pad < 0) {
            return std::nullopt;
        }
        const StaticShape &s = *x.shape;
        for (size_t a : {size_t{2}, size_t{3}}) {
            if (s[a] != kUnknownDim && s[a] + 2 * *pad < *kernel) {
                return std::nullopt; // the window does not fit: the kernel rejects it
            }
        }
        IndexBuilder b;
        auto &e             = b.x();
        const ExprId K      = b.constant("kernel", 1, *kernel);
        const ExprId S      = ctx.has(2) ? b.constant("stride", 2, *stride) : K;
        const ExprId P      = ctx.has(3) ? b.constant("pad", 3, *pad) : e.constant(0);
        const ExprId N      = b.extent(0, 0, s[0]), C = b.extent(0, 1, s[1]);
        const ExprId H      = b.extent(0, 2, s[2]), W = b.extent(0, 3, s[3]);
        b.require(e.constant(1), Rel::Le, S);
        auto outExtent = [&](ExprId in, int64_t known, const char *name) {
            const ExprId def =
                e.add(e.floorDiv(e.sub(e.add(in, e.scale(2, P)), K), S), e.constant(1));
            const std::optional<int64_t> value =
                known == kUnknownDim ? std::nullopt
                                     : std::optional((known + 2 * *pad - *kernel) / *stride + 1);
            return b.derived(name, value, def);
        };
        const ExprId OH = outExtent(H, s[2], "oh"), OW = outExtent(W, s[3], "ow");
        const ExprId n = b.dim(N), c = b.dim(C), oh = b.dim(OH), ow = b.dim(OW);
        const ExprId kh = b.dim(K, sem::DimRole::Reduction), kw = b.dim(K, sem::DimRole::Reduction);
        const ExprId ih = e.sub(e.add(e.mul(S, oh), kh), P);
        const ExprId iw = e.sub(e.add(e.mul(S, ow), kw), P);
        sem::Access read = IndexBuilder::affine({n, c, ih, iw});
        read.valid       = {
            {e.constant(0), Rel::Le, ih}, {ih, Rel::Lt, H}, {e.constant(0), Rel::Le, iw}, {iw, Rel::Lt, W}};
        read.onInvalid = max ? sem::OnInvalid::Excluded : sem::OnInvalid::Padding;
        read.padding   = 0; // +0.0f
        b.instance.norm.push_back(std::move(read));
        for (size_t i = 1; i < ctx.size(); ++i) {
            b.instance.norm.push_back(IndexBuilder::none());
        }
        return b.finish({n, c, oh, ow}, sem::WriteMode::Combine, 0);
    };
}

/// The semantics of max_pool2d (max) or avg_pool2d: numeric, and the laws of poolIndexing.
camel::core::sem::OperatorSemantics poolSemantics(bool max) {
    namespace sem = camel::core::sem;
    sem::OperatorSemantics s;
    if (max) {
        s.numeric = sem::NumericSemantics{
            .cls         = sem::NumericClass::Exact,
            .compute     = TypeCode::Float32,
            .sensitiveTo = sem::kFiniteMath,
            .algorithm   = "IEEE 754-2019 maximum over the window's elements inside the input, "
                           "from -inf (a window entirely in the padding gives -inf)",
        };
        s.laws = {sem::ReductionLaw{
            .name             = "max_pool_f32",
            .kind             = sem::LawKind::Max,
            .input            = TypeCode::Float32,
            .accumulator      = TypeCode::Float32,
            .result           = TypeCode::Float32,
            .initBits         = 0xff800000u,
            .exactAssociative = true,
            .exactCommutative = true,
            .hasIdentity      = true,
            .identityBits     = 0xff800000u,
        }};
    } else {
        s.numeric = sem::NumericSemantics{
            .cls         = sem::NumericClass::CorrectlyRounded,
            .compute     = TypeCode::Float64,
            .sensitiveTo = sem::kReassociate | sem::kChangePrecision | sem::kFiniteMath,
            .algorithm   = "the window's elements (+0 for those in the padding) added to an f64 "
                           "accumulator from +0 in row-major window order, divided by "
                           "kernel*kernel in f64, rounded once to f32",
        };
        s.laws = {sem::ReductionLaw{
            .name             = "avg_pool_f32",
            .kind             = sem::LawKind::Add,
            .input            = TypeCode::Float32,
            .accumulator      = TypeCode::Float64,
            .result           = TypeCode::Float32,
            .finalize         = sem::LawFinalize::DivideByCount,
            .exactCommutative = true,
            .hasIdentity      = true,
            .identityBits     = 0x8000000000000000u,
        }};
    }
    return s;
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
slot_t tripleOf(Array *first, Array *second, Array *third) {
    namespace mm          = camel::core::mm;
    const auto tensorType = [](const Array *t) -> Type * {
        StaticShape shape(t->shapeSpan().begin(), t->shapeSpan().end());
        return TensorType::get(t->dtype(), std::move(shape));
    };
    TupleType *type = TupleType::create({tensorType(first), tensorType(second), tensorType(third)});
    auto *tuple     = ::Tuple::create(3, mm::autoSpace());
    tuple->set<slot_t>(0, camel::core::rtdata::toSlot<Array *>(first), type);
    tuple->set<slot_t>(1, camel::core::rtdata::toSlot<Array *>(second), type);
    tuple->set<slot_t>(2, camel::core::rtdata::toSlot<Array *>(third), type);
    return camel::core::rtdata::toSlot<::Tuple *>(tuple);
}

slot_t batchNormTrainKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("batch_norm_train", [&] {
        const double momentum = hasArg(norm, 5) ? numberArg(norm, 5) : 0.1;
        const double eps      = hasArg(norm, 6) ? numberArg(norm, 6) : 1e-5;
        Array *y, *mean, *var;
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
        Array *dx, *dgamma, *dbeta;
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

/// Checked before the labels kernels: a label outside [0, classes) is a located fault (design
/// principles 3.9), and a label count that disagrees with the batch a shape error.
void checkClassLabels(
    const Array *logits, const std::vector<int64_t> &labels, std::string_view op) {
    if (logits->rank() != 2) {
        reportShapeError(op, "expects rank-2 logits [batch, classes]");
    }
    const int64_t rows = logits->dim(0), cols = logits->dim(1);
    if (static_cast<int64_t>(labels.size()) != rows) {
        reportShapeError(op, "requires one label per logits row");
    }
    for (const int64_t label : labels) {
        if (label < 0 || label >= cols) {
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                label,
                std::string("the class dimension of ") + std::string(op),
                cols);
        }
    }
}

slot_t softmaxCrossEntropyLabelsKernel(ArgsView &, ArgsView &norm, context::Context &) {
    // The label checks are located faults of their own, not kernel failures (design principles
    // 3.9): they run outside runKernel, which maps exceptions to TensorDimensionMismatch.
    const Array *logits        = tensorArg(norm, 0);
    const std::vector<int64_t> labels = labelArg(norm, 1, "softmax_cross_entropy");
    checkClassLabels(logits, labels, "softmax_cross_entropy");
    return runKernel("softmax_cross_entropy_labels", [&] {
        return camel::core::rtdata::toSlot(softmaxCrossEntropyLabels(logits, labels));
    });
}

slot_t softmaxCrossEntropyLabelsGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *logits        = tensorArg(norm, 0);
    const std::vector<int64_t> labels = labelArg(norm, 1, "softmax_cross_entropy_grad");
    checkClassLabels(logits, labels, "softmax_cross_entropy_grad");
    return runKernel("softmax_cross_entropy_labels_grad", [&] {
        return wrap(softmaxCrossEntropyLabelsGradLogits(
            logits, labels, numberArg(norm, 2), resultAllocator()));
    });
}

/// The labels form is chosen when the target is known to be integer. A target whose element
/// type is not fixed (a bare `Tensor`) keeps the one-hot form: it is the existing spelling.
std::optional<Type *> softmaxCrossEntropyLabelsInfer(const InferContext &ctx) {
    const TensorFacts target = ctx.facts(1);
    if (!target.dtype || !isIntegerCode(target.dtype)) {
        return std::nullopt;
    }
    return Type::Float64();
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

void softmaxCrossEntropyLabelsVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(
                b,
                "nn:softmax_cross_entropy_labels_grad",
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
            .traits    = kPure,
            .semantics = poolSemantics(true),
            .index     = poolIndexing(true)});
    defs.push_back(
        OpDef{
            .name      = "avg_pool2d",
            .exports   = {"avg_pool2d"},
            .params    = poolParams(false),
            .resultDoc = "Tensor",
            .infer     = poolInfer,
            .kernel    = &poolKernel<k::PoolKind::Average>,
            .traits    = kPure,
            .semantics = poolSemantics(false),
            .index     = poolIndexing(false)});
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
            .infer  = [](const InferContext &ctx) -> std::optional<Type *> {
                const TensorFacts target = ctx.facts(1);
                if (target.dtype && isIntegerCode(target.dtype)) {
                    return std::nullopt; // integer targets select the labels overload
                }
                return Type::Float64();
            },
            .kernel = &softmaxCrossEntropyKernel,
            .traits = kPure});
    defs.push_back(
        OpDef{
            .name      = "softmax_cross_entropy_grad",
            .exports   = {"softmax_cross_entropy_grad"},
            .params    = {{"logits", ParamKind::Tensor},
                          {"target", ParamKind::Tensor},
                          {"dy", ParamKind::Number}},
            .resultDoc = "Tensor",
            .infer     = [](const InferContext &ctx) -> std::optional<Type *> {
                const TensorFacts target = ctx.facts(1);
                if (target.dtype && isIntegerCode(target.dtype)) {
                    return std::nullopt; // integer targets select the labels overload
                }
                return tensorOf(TypeCode::Float32, ctx.facts(0).shape);
            },
            .kernel = &softmaxCrossEntropyGradKernel,
            .traits = kPure});
    defs.push_back(
        OpDef{
            .name      = "softmax_cross_entropy_labels",
            .exports   = {"softmax_cross_entropy"},
            .params    = {{"logits", ParamKind::Tensor}, {"target", ParamKind::TensorLike}},
            .resultDoc = "float",
            .infer     = &softmaxCrossEntropyLabelsInfer,
            .kernel    = &softmaxCrossEntropyLabelsKernel,
            .traits    = kPure});
    defs.push_back(
        OpDef{
            .name      = "softmax_cross_entropy_labels_grad",
            .exports   = {"softmax_cross_entropy_grad"},
            .params    = {{"logits", ParamKind::Tensor},
                          {"target", ParamKind::TensorLike},
                          {"dy", ParamKind::Number}},
            .resultDoc = "Tensor",
            .infer     = [](const InferContext &ctx) -> std::optional<Type *> {
                const TensorFacts target = ctx.facts(1);
                if (!target.dtype || !isIntegerCode(target.dtype)) {
                    return std::nullopt;
                }
                return tensorOf(TypeCode::Float32, ctx.facts(0).shape);
            },
            .kernel = &softmaxCrossEntropyLabelsGradKernel,
            .traits = kPure});
    setVjp(defs, "conv2d", &conv2dVjp);
    setVjp(defs, "batch_norm_train", &batchNormTrainVjp);
    setVjp(defs, "max_pool2d", &pool2dVjp<kMaxPoolGradUri>);
    setVjp(defs, "avg_pool2d", &pool2dVjp<kAvgPoolGradUri>);
    setVjp(defs, "embedding", &embeddingVjp);
    setVjp(defs, "softmax_cross_entropy", &softmaxCrossEntropyVjp);
    setVjp(defs, "softmax_cross_entropy_labels", &softmaxCrossEntropyLabelsVjp);
    return defs;
}

} // namespace

void registerNnTensorOps() {
    static std::once_flag once;
    std::call_once(once, [] { OpRegistry::instance().add("nn", nnTensorOps()); });
}

} // namespace camel::nn
