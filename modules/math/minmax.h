// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * The two IEEE 754-2019 comparisons the `math` module's `min` and `max` are, for every floating
 * type they are defined on (design principles 7.7): a NaN on either side gives the canonical quiet
 * NaN, and -0 is below +0 — equal values are identical except for the sign of zero, which decides
 * between them. `numeric.cpp` uses them for f32 and f64, `operators.cpp` for bf16 and f16, and the
 * two must not be written twice: the reduced-precision overloads are the same operation on the
 * value each operand widens to, so a second copy could only drift.
 *
 * They are the operation the tensor module's `min`/`max` kernels are as well
 * (modules/tensor/kernels/minmax.h).
 */

#include <cmath>
#include <limits>
#include <type_traits>

namespace camel::core::math {

template <typename T> T minOf(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();
        if (a == b)
            return std::signbit(a) ? a : b;
    }
    return b < a ? b : a;
}

template <typename T> T maxOf(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();
        if (a == b)
            return std::signbit(a) ? b : a;
    }
    return a < b ? b : a;
}

} // namespace camel::core::math