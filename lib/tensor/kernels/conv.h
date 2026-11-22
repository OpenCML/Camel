// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Spatial kernels on NCHW float tensors: 2-D convolution (forward and the
 * three gradients), max/average pooling (forward and gradient), and
 * inference-mode batch normalization.
 *
 * Convolution is cross-correlation (no kernel flip) implemented as
 * im2col + GEMM: each image is unfolded into a [C*KH*KW, OH*OW] column
 * matrix and multiplied by the [O, C*KH*KW] kernel matrix. Gradients reuse
 * the same unfolding (col2im scatters input gradients back).
 */

#pragma once

#include "../tensor.h"

namespace camel::tensor::kernels {

struct Window2d {
    int64_t kernelH = 1, kernelW = 1;
    int64_t strideH = 1, strideW = 1;
    int64_t padH = 0, padW = 0;

    /// Output extent along H (outH) or W (outW) for an input extent `in`.
    int64_t outH(int64_t in) const { return (in + 2 * padH - kernelH) / strideH + 1; }
    int64_t outW(int64_t in) const { return (in + 2 * padW - kernelW) / strideW + 1; }
};

/// Output shape [N, O, OH, OW] of conv2d. Validates ranks, channels, and window fit.
Shape conv2dShape(
    std::span<const int64_t> input, std::span<const int64_t> kernel, int64_t stride,
    int64_t padding);

/// y = conv2d(x, w) + b. `bias` may be null.
Array *conv2d(
    const Array *input, const Array *kernel, const Array *bias, int64_t stride,
    int64_t padding, mm::IAllocator &allocator);

/// dL/dx given dL/dy.
Array *conv2dInputGrad(
    const Array *input, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator);

/// dL/dw given dL/dy.
Array *conv2dKernelGrad(
    const Array *input, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator);

/// dL/db given dL/dy: sum over N, OH, OW.
Array *conv2dBiasGrad(const Array *dy, mm::IAllocator &allocator);

enum class PoolKind { Max, Average };

/// Output shape [N, C, OH, OW] of pooling.
Shape pool2dShape(std::span<const int64_t> input, const Window2d &window);

Array *
pool2d(PoolKind kind, const Array *input, const Window2d &window, mm::IAllocator &allocator);

/// dL/dx for pooling. Max routes each gradient to the first maximal element of its window.
Array *pool2dGrad(
    PoolKind kind, const Array *input, const Array *dy, const Window2d &window,
    mm::IAllocator &allocator);

/// y = gamma * (x - mean) / sqrt(var + eps) + beta per channel (axis 1).
Array *batchNorm(
    const Array *input, const Array *mean, const Array *var,
    const Array *gamma, const Array *beta, double eps, mm::IAllocator &allocator);

/// Training-mode batch normalization: the batch statistics are computed per channel over the
/// batch and the trailing (spatial) axes. The normalize uses the BIASED batch variance; the
/// running statistics move toward the batch statistics by `momentum`, the variance toward the
/// UNBIASED batch variance (M/(M-1) correction) — PyTorch's training-mode semantics. Writes y,
/// the new running mean and the new running variance (all freshly allocated).
void batchNormTrain(
    const Array *input, const Array *gamma, const Array *beta,
    const Array *runningMean, const Array *runningVar, double momentum, double eps,
    Array **outY, Array **outRunningMean, Array **outRunningVar,
    mm::IAllocator &allocator);

/// Gradients of batchNormTrain wrt the input and the affine parameters:
/// dbeta = sum(dy), dgamma = sum(dy*xhat), dx = gamma*inv*(dy - mean(dy) - xhat*mean(dy*xhat))
/// per channel over the batch and spatial axes (xhat the normalized input, inv = 1/sqrt(var+eps)
/// with the biased batch variance). Writes dx, dgamma, dbeta (freshly allocated).
void batchNormTrainGrad(
    const Array *input, const Array *gamma, const Array *dy, double eps,
    Array **outDx, Array **outDgamma, Array **outDbeta,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
