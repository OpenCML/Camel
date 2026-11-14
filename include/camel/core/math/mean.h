// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The normative form of `mean` (design principles, the entry for `sum`/`mean`: `mean` is
 * `T(sum(float(x)) / float(numel(x)))`, accumulated and divided in f64, rounded once).
 *
 * It lives here because two implementations ask for it: the `array` module's axis reduction
 * (src/builtin/operators/array_axis.cpp) and the `tensor` module's kernel
 * (modules/tensor/kernels/reduce.cpp). The interpreter has one mean, so the formula is written
 * once and both call it -- a second copy is how the two paths came to disagree about integers
 * (one stored the f32 quotient into a slot typed by the element type, the other refused).
 *
 * `static_cast<T>` is the element type's own conversion: for `f32`, `f64`, `bf16` and `f16` it
 * rounds once into the element type, and for `int`/`i32` it is the conversion `int(·)` already
 * has -- toward zero. An integer mean is therefore `int(7 / 3) = 2`, which is what the
 * normative form says, not a lost average: a program that wants a floating mean writes
 * `float(xs)` first.
 *
 * A lane with no element divides `0.0` by `0.0`: the canonical NaN for a floating element type.
 */
#pragma once

#include <cstdint>

namespace camel::core::math {

/// `T(sum / numel)`: `total` is the lane's sum accumulated in f64 from +0, `count` its extent.
template <typename T> T meanValue(double total, int64_t count) {
    const double mean = total / static_cast<double>(count);
    return static_cast<T>(mean);
}

} // namespace camel::core::math