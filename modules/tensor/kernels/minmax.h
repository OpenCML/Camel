// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The maximum, minimum and relu of every tensor kernel (design principles 7.7: `max` and
 * `min` are IEEE 754-2019 maximum and minimum, and relu(x) = maximum(x, +0)).
 *
 * For floats:
 *   - a NaN on either side gives NaN. The result is always the canonical quiet NaN
 *     (std::numeric_limits<T>::quiet_NaN(), sign bit clear, no payload), whatever NaN came in,
 *     so the operation is exactly commutative and associative bit for bit and a max or min
 *     reduction gives the same bits in every order and every grouping, without a permission;
 *   - -0 is below +0: maximum(-0, +0) = +0 and minimum(-0, +0) = -0 on either side;
 *   - otherwise the larger (smaller) operand.
 * For integers and bool they are the plain larger and smaller value.
 *
 * Every operator of the tensor module that takes a maximum or a minimum (the elementwise
 * `maximum`/`minimum`, the `max`/`min` reductions, max pooling, the stabilising maximum of
 * softmax and relu) goes through these functions, so they cannot drift apart. They are
 * branch-free selects, which the compiler vectorizes inside loops.
 */

#pragma once

#include "camel/core/rtdata/half.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace camel::tensor::kernels {

namespace detail {
/// The unsigned integer type of the bits of a float type.
template <typename T> using FloatBits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
} // namespace detail

/// IEEE 754-2019 maximum (see the file comment).
template <typename T> inline T maximum(T a, T b) {
    if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
        // An f16 or bf16 maximum is the f32 maximum of two exactly converted operands,
        // converted back: the result is one of the two values, so no rounding happens.
        return T(maximum(a.toFloat(), b.toFloat()));
    } else if constexpr (std::is_floating_point_v<T>) {
        using Bits = detail::FloatBits<T>;
        // Equal operands are identical except +0 and -0: the AND of the bits keeps the sign
        // bit only when both are -0, which picks +0 from a mixed pair.
        const T equal = std::bit_cast<T>(std::bit_cast<Bits>(a) & std::bit_cast<Bits>(b));
        const T r     = a == b ? equal : (a < b ? b : a);
        return (a != a || b != b) ? std::numeric_limits<T>::quiet_NaN() : r;
    } else {
        return a < b ? b : a;
    }
}

/// IEEE 754-2019 minimum (see the file comment).
template <typename T> inline T minimum(T a, T b) {
    if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
        return T(minimum(a.toFloat(), b.toFloat()));
    } else if constexpr (std::is_floating_point_v<T>) {
        using Bits = detail::FloatBits<T>;
        // The OR of the bits of equal operands sets the sign bit when either is -0.
        const T equal = std::bit_cast<T>(std::bit_cast<Bits>(a) | std::bit_cast<Bits>(b));
        const T r     = a == b ? equal : (b < a ? b : a);
        return (a != a || b != b) ? std::numeric_limits<T>::quiet_NaN() : r;
    } else {
        return b < a ? b : a;
    }
}

/**
 * argmax (Max) and argmin (design principles 7.7): true when `v`, met after `best` (the
 * extreme so far), takes its place. The order is the one of IEEE 754-2019 maximum and minimum:
 * a NaN is beyond every number (so the first NaN is kept), +0 is above -0, and on a tie the
 * earlier position stays. The result is the position of the first element equal to the
 * maximum (minimum) of the whole sequence, or of its first NaN.
 */
template <bool Max, typename T> inline bool beyondExtreme(T v, T best) {
    if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
        return beyondExtreme<Max>(v.toFloat(), best.toFloat());
    } else if constexpr (std::is_floating_point_v<T>) {
        if (best != best) {
            return false;
        }
        if (v != v) {
            return true;
        }
        if (v == best) {
            // Only +0 and -0 compare equal with different bits.
            return v == 0 && (Max ? (std::signbit(best) && !std::signbit(v))
                                  : (!std::signbit(best) && std::signbit(v)));
        }
    }
    return Max ? best < v : v < best;
}

/// relu(x) = maximum(x, +0): NaN gives the canonical NaN, -0 gives +0.
template <typename T> inline T relu(T x) { return maximum(x, T{}); }

/// The value a maximum reduction starts from: the identity of `maximum` (-inf for floats, so
/// a single NaN element still becomes the canonical NaN), the lowest value otherwise.
template <typename T> constexpr T maximumIdentity() {
    if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
        return T(-std::numeric_limits<float>::infinity());
    } else if constexpr (std::is_floating_point_v<T>) {
        return -std::numeric_limits<T>::infinity();
    } else {
        return std::numeric_limits<T>::lowest();
    }
}

/// The value a minimum reduction starts from: +inf for floats, the largest value otherwise.
template <typename T> constexpr T minimumIdentity() {
    if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
        return T(std::numeric_limits<float>::infinity());
    } else if constexpr (std::is_floating_point_v<T>) {
        return std::numeric_limits<T>::infinity();
    } else {
        return std::numeric_limits<T>::max();
    }
}

} // namespace camel::tensor::kernels
