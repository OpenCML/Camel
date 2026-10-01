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
TensorObject *conv2d(
    const TensorObject *input, const TensorObject *kernel, const TensorObject *bias, int64_t stride,
    int64_t padding, mm::IAllocator &allocator);

/// dL/dx given dL/dy.
TensorObject *conv2dInputGrad(
    const TensorObject *input, const TensorObject *kernel, const TensorObject *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator);

/// dL/dw given dL/dy.
TensorObject *conv2dKernelGrad(
    const TensorObject *input, const TensorObject *kernel, const TensorObject *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator);

/// dL/db given dL/dy: sum over N, OH, OW.
TensorObject *conv2dBiasGrad(const TensorObject *dy, mm::IAllocator &allocator);

enum class PoolKind { Max, Average };

/// Output shape [N, C, OH, OW] of pooling.
Shape pool2dShape(std::span<const int64_t> input, const Window2d &window);

TensorObject *
pool2d(PoolKind kind, const TensorObject *input, const Window2d &window, mm::IAllocator &allocator);

/// dL/dx for pooling. Max routes each gradient to the first maximal element of its window.
TensorObject *pool2dGrad(
    PoolKind kind, const TensorObject *input, const TensorObject *dy, const Window2d &window,
    mm::IAllocator &allocator);

/// y = gamma * (x - mean) / sqrt(var + eps) + beta per channel (axis 1).
TensorObject *batchNorm(
    const TensorObject *input, const TensorObject *mean, const TensorObject *var,
    const TensorObject *gamma, const TensorObject *beta, double eps, mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
