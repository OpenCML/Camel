// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
Array *
sumTo(const Array *g, std::span<const int64_t> shape, mm::IAllocator &allocator);

/// Broadcasts `t` to `shape` (numpy rules).
Array *
broadcastTo(const Array *t, std::span<const int64_t> shape, mm::IAllocator &allocator);

/// A scalar of element type `dtype` broadcast to `shape`. The element type is the one the value is
/// made like, not the scalar's: the result is a gradient of that value and is read at the width its
/// type names, so filling at any other width would put a number of one width behind a type of
/// another.
Array *
fill(double value, type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator);

/// Gradient of a reduction over `axis` of an input of `shape`: `g` re-expanded along the axis.
Array *expandAxis(
    const Array *g, std::span<const int64_t> shape, int64_t axis, bool keepDims,
    mm::IAllocator &allocator);

/// dy * gelu'(x) for the tanh approximation.
Array *geluGrad(const Array *x, const Array *dy, mm::IAllocator &allocator);

/// dy * erf'(x).
Array *erfGrad(const Array *x, const Array *dy, mm::IAllocator &allocator);

enum class LayerNormGrad { Input, Gamma, Beta };

/// One gradient of layer_norm(x, gamma, beta, eps) given dy.
Array *layerNormGrad(
    const Array *x, const Array *gamma, const Array *dy, double eps,
    LayerNormGrad which, mm::IAllocator &allocator);

/// Gradient of slice(t, axis, start, end, step) for an input of `shape`: dy scattered into zeros.
Array *sliceGrad(
    const Array *dy, std::span<const int64_t> shape, int64_t axis, int64_t start,
    int64_t end, int64_t step, mm::IAllocator &allocator);

/// Gradients of matmul(lhs, rhs) given dy, reduced over broadcast batch dimensions. `useLibrary`
/// is the forward node's `tensor.kernel` choice carried to the gradient; the library GEMM itself
/// falls back to the canonical kernel below the measured shape threshold (gemm.cpp).
Array *matmulGradLhs(
    const Array *dy, const Array *lhs, const Array *rhs, bool useLibrary,
    mm::IAllocator &allocator);
Array *matmulGradRhs(
    const Array *dy, const Array *lhs, const Array *rhs, bool useLibrary,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
