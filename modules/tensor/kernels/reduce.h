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
 * Axis reductions and row-wise normalizations.
 *
 * A reduction over axis `a` views the input as [outer, extent(a), inner] and
 * reduces the middle dimension. Negative axes count from the end. With
 * `keepDims` the reduced axis stays as extent 1; otherwise it is removed.
 *
 * Float sums accumulate in double so results do not depend on the thread
 * split or on summation order within a row.
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
};

/// Normalizes a possibly negative axis against `rank`; throws when out of range.
size_t normalizeAxis(int64_t axis, size_t rank);

/// Shape after reducing `axis`.
Shape reducedShape(std::span<const int64_t> shape, size_t axis, bool keepDims);

/**
 * Reduces one axis. Result dtypes: Sum/Max/Min keep the input dtype (bool sums
 * become int64), Mean is float32, ArgMax is int64.
 */
TensorObject *reduceAxis(
    ReduceOp op, const TensorObject *input, int64_t axis, bool keepDims, mm::IAllocator &allocator);

/// Sum of all elements as double.
double sumAll(const TensorObject *input);

/// Softmax along `axis` (float32 result).
TensorObject *softmax(const TensorObject *input, int64_t axis, mm::IAllocator &allocator);
/// Log-softmax along `axis` (float32 result).
TensorObject *logSoftmax(const TensorObject *input, int64_t axis, mm::IAllocator &allocator);
/// Backward of softmax along `axis`: dx = y * (dy - sum(dy * y, axis)).
TensorObject *softmaxGrad(
    const TensorObject *output, const TensorObject *dy, int64_t axis, mm::IAllocator &allocator);

/**
 * Layer normalization over the trailing dimensions covered by `gamma`/`beta`
 * (both shaped like the normalized suffix of `input`).
 */
TensorObject *layerNorm(
    const TensorObject *input, const TensorObject *gamma, const TensorObject *beta, double eps,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
