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
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <concepts>
#include <type_traits>

namespace camel::utils {

/// The quotient `lhs / rhs` of the language, `rhs` not zero (the caller raises DivisionByZero).
/// One integer quotient does not fit its type: the smallest value divided by -1. The C++ `/` is
/// undefined there and traps on x86 (SIGFPE). Here it wraps around to the smallest value, as
/// the sums and products of the language wrap.
template <std::signed_integral T> constexpr T quotient(T lhs, T rhs) {
    if (rhs == static_cast<T>(-1)) {
        using U = std::make_unsigned_t<T>;
        return static_cast<T>(static_cast<U>(0) - static_cast<U>(lhs));
    }
    return static_cast<T>(lhs / rhs);
}

template <std::floating_point T> constexpr T quotient(T lhs, T rhs) { return lhs / rhs; }

/// The remainder `lhs % rhs` of the language, `rhs` not zero. Every remainder by -1 is 0; the
/// C++ `%` of the smallest value by -1 is undefined and traps like the quotient.
template <std::signed_integral T> constexpr T remainder(T lhs, T rhs) {
    return rhs == static_cast<T>(-1) ? static_cast<T>(0) : static_cast<T>(lhs % rhs);
}

} // namespace camel::utils
