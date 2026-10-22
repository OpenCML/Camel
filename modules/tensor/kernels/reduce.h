// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Axis reductions and row-wise normalizations.
 *
 * A reduction over axis `a` views the input as [outer, extent(a), inner] and
 * reduces the middle dimension. Negative axes count from the end. With
 * `keepDims` the reduced axis stays as extent 1; otherwise it is removed.
 *
 * Float sums accumulate in double from +0 in ascending order; int64 and bool
 * sums accumulate exactly in int64, wrapping on overflow (design principles
 * 5.1, 7.7). The thread split never changes a result: each lane is summed
 * by one thread.
 */

#pragma once

#include "../tensor.h"

namespace camel::tensor::kernels {

enum class ReduceOp {
    Sum,
    Mean,
    Max,
    Min,
    ArgMax,
    ArgMin,
};

/// Normalizes a possibly negative axis against `rank`; throws when out of range.
size_t normalizeAxis(int64_t axis, size_t rank);

/// Shape after reducing `axis`.
Shape reducedShape(std::span<const int64_t> shape, size_t axis, bool keepDims);

/**
 * Reduces one axis. Result dtypes: Sum/Max/Min keep the input dtype (bool sums
 * become int64), Mean is float32, ArgMax and ArgMin are int64.
 */
Array *reduceAxis(
    ReduceOp op, const Array *input, int64_t axis, bool keepDims, mm::IAllocator &allocator);

/// Sum of all elements as double: f32 elements accumulated in f64 in row-major order; int64
/// and bool elements accumulated exactly in int64 (wrapping), converted to double once.
double sumAll(const Array *input);

/// Softmax along `axis` (float32 result).
Array *softmax(const Array *input, int64_t axis, mm::IAllocator &allocator);
/// Log-softmax along `axis` (float32 result).
Array *logSoftmax(const Array *input, int64_t axis, mm::IAllocator &allocator);
/// Backward of softmax along `axis`: dx = y * (dy - sum(dy * y, axis)).
Array *softmaxGrad(
    const Array *output, const Array *dy, int64_t axis, mm::IAllocator &allocator);

/**
 * Layer normalization over the trailing dimensions covered by `gamma`/`beta`
 * (both shaped like the normalized suffix of `input`).
 */
Array *layerNorm(
    const Array *input, const Array *gamma, const Array *beta, double eps,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
