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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The maximum, minimum and relu of every tensor kernel (design principles 2.13: `max` and
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
 * softmax, relu and the fused matmul_add_relu) goes through these functions, so they cannot
 * drift apart. They are branch-free selects, which the compiler vectorizes inside loops.
 */

#pragma once

#include <bit>
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
    if constexpr (std::is_floating_point_v<T>) {
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
    if constexpr (std::is_floating_point_v<T>) {
        using Bits = detail::FloatBits<T>;
        // The OR of the bits of equal operands sets the sign bit when either is -0.
        const T equal = std::bit_cast<T>(std::bit_cast<Bits>(a) | std::bit_cast<Bits>(b));
        const T r     = a == b ? equal : (b < a ? b : a);
        return (a != a || b != b) ? std::numeric_limits<T>::quiet_NaN() : r;
    } else {
        return b < a ? b : a;
    }
}

/// relu(x) = maximum(x, +0): NaN gives the canonical NaN, -0 gives +0.
template <typename T> inline T relu(T x) { return maximum(x, T{}); }

/// The value a maximum reduction starts from: the identity of `maximum` (-inf for floats, so
/// a single NaN element still becomes the canonical NaN), the lowest value otherwise.
template <typename T> constexpr T maximumIdentity() {
    if constexpr (std::is_floating_point_v<T>) {
        return -std::numeric_limits<T>::infinity();
    } else {
        return std::numeric_limits<T>::lowest();
    }
}

/// The value a minimum reduction starts from: +inf for floats, the largest value otherwise.
template <typename T> constexpr T minimumIdentity() {
    if constexpr (std::is_floating_point_v<T>) {
        return std::numeric_limits<T>::infinity();
    } else {
        return std::numeric_limits<T>::max();
    }
}

} // namespace camel::tensor::kernels
