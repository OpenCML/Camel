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
 * Kernels of the operators that reverse-mode rules emit: reducing a gradient
 * back to a broadcast operand's shape, broadcasting a reduced gradient back
 * to its input, and the backward passes that are not compositions of forward
 * operators (gelu, layer_norm, slice, matmul with broadcast batch dims).
 *
 * Gradients are float32, like the forward kernels' float results.
 */

#pragma once

#include "../tensor.h"

namespace camel::tensor::kernels {

/// Sums `g` over the axes along which a tensor of `shape` was broadcast to g's shape.
TensorObject *
sumTo(const TensorObject *g, std::span<const int64_t> shape, mm::IAllocator &allocator);

/// Broadcasts `t` to `shape` (numpy rules).
TensorObject *
broadcastTo(const TensorObject *t, std::span<const int64_t> shape, mm::IAllocator &allocator);

/// A scalar broadcast to `shape`.
TensorObject *fill(double value, std::span<const int64_t> shape, mm::IAllocator &allocator);

/// Gradient of a reduction over `axis` of an input of `shape`: `g` re-expanded along the axis.
TensorObject *expandAxis(
    const TensorObject *g, std::span<const int64_t> shape, int64_t axis, bool keepDims,
    mm::IAllocator &allocator);

/// dy * gelu'(x) for the tanh approximation.
TensorObject *geluGrad(const TensorObject *x, const TensorObject *dy, mm::IAllocator &allocator);

/// dy * erf'(x).
TensorObject *erfGrad(const TensorObject *x, const TensorObject *dy, mm::IAllocator &allocator);

enum class LayerNormGrad { Input, Gamma, Beta };

/// One gradient of layer_norm(x, gamma, beta, eps) given dy.
TensorObject *layerNormGrad(
    const TensorObject *x, const TensorObject *gamma, const TensorObject *dy, double eps,
    LayerNormGrad which, mm::IAllocator &allocator);

/// Gradient of slice(t, axis, start, end, step) for an input of `shape`: dy scattered into zeros.
TensorObject *sliceGrad(
    const TensorObject *dy, std::span<const int64_t> shape, int64_t axis, int64_t start,
    int64_t end, int64_t step, mm::IAllocator &allocator);

/// Gradients of matmul(lhs, rhs) given dy, reduced over broadcast batch dimensions.
TensorObject *matmulGradLhs(
    const TensorObject *dy, const TensorObject *lhs, const TensorObject *rhs,
    mm::IAllocator &allocator);
TensorObject *matmulGradRhs(
    const TensorObject *dy, const TensorObject *lhs, const TensorObject *rhs,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
