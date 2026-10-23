// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Axis reductions and row-wise normalizations.
 *
 * A reduction over axis `a` views the input as [outer, extent(a), inner] and
 * reduces the middle dimension. Negative axes count from the end. With
 * `keepDims` the reduced axis stays as extent 1; otherwise it is removed.
 *
 * The accumulation type of each reduction, so a reader does not have to re-derive it:
 *
 *   - Sum and Mean of a floating element type (f32, f64, bf16, f16) accumulate in
 *     `double` from +0 in ascending order and round once, when the element is
 *     stored back into the result's element type. This is the rule of design
 *     principles 7.3 ("浮点数组的归约在 f64 中累加、舍入一次，返回元素类型") and the ruling
 *     docs/handover/reviews/reduced-precision.md (d); `array::sum` in stdlib/array.cml
 *     follows it too, so the array path and this one agree bit for bit.
 *   - Sum and Mean of an integer or bool element type have no float accumulator at
 *     all: they accumulate exactly in int64 (on uint64_t, where wrapping is
 *     defined) and convert once, as design principles 7.7 requires.
 *   - Max, Min, ArgMax and ArgMin have no accumulator: they carry an element
 *     value, and IEEE 754-2019 maximum/minimum are exactly associative and
 *     commutative (7.7), so no order and no width can change a result.
 *   - `sumAll` (the whole-tensor `sum`) answers f64: f64 accumulation, no
 *     rounding back into a narrow element type.
 *
 * The thread split never changes a result: each lane is summed by one thread.
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
