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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Complex numbers in the programs std::cpp writes: `c64` is `std::complex<float>` and `c128` is
 * `std::complex<double>`.
 *
 * The functions here do what the runtime's operators do (src/builtin/operators/ops.cpp and
 * modules/math/operators.cpp), including where that is a fault:
 *
 *   - +, -, * and unary - are std::complex's, the same arithmetic the runtime's
 *     complexBinary computes in the width of the parts;
 *   - / is DivisionByZero at the site for a zero divisor (std::complex's own division would
 *     give an infinity);
 *   - `z ^ w` is the runtime's complexPower: a whole exponent is repeated squaring (so `z ^ 2`
 *     is exactly `z * z`, and a negative one takes the reciprocal), any other exponent is
 *     std::pow;
 *   - a number becomes a complex value with a zero imaginary part, and a complex value becomes
 *     the other width with each part converted (camel/core/numeric_convert.h);
 *   - the text is camel::utils::complexText (`3+4i`, `3-4i`), as the runtime prints it.
 *
 * `re`, `im`, `conj` and `abs` name the runtime's `math` operators; `exp` is std::exp, the same
 * function the module calls.
 */

#pragma once

#include "camel/standalone/core.h"
#include "camel/utils/number_text.h"

#include <cmath>
#include <complex>
#include <limits>
#include <string>

namespace camel::standalone {

/// The text of a complex number (`3+4i`): what println writes without a format.
inline void appendText(std::string &out, const std::complex<float> &value) {
    out += camel::utils::complexText<float>(value.real(), value.imag());
}
inline void appendText(std::string &out, const std::complex<double> &value) {
    out += camel::utils::complexText<double>(value.real(), value.imag());
}

/// `a / b` (:op/div_c, :op/div_z): a zero divisor is DivisionByZero at `site`.
template <typename T>
std::complex<T> complexDiv(std::complex<T> a, std::complex<T> b, const Site &site) {
    if (b == std::complex<T>{}) [[unlikely]] {
        fault(site);
    }
    return a / b;
}

namespace detail {

/// `z ^ n` with an integer n: repeated squaring, as the runtime's integerPower.
template <typename T> std::complex<T> integerPower(const std::complex<T> &base, long long exponent) {
    const bool negative     = exponent < 0;
    unsigned long long rest = negative ? 0ULL - static_cast<unsigned long long>(exponent)
                                       : static_cast<unsigned long long>(exponent);
    std::complex<T> result{1, 0};
    std::complex<T> factor = base;
    while (rest != 0) {
        if ((rest & 1ULL) != 0) {
            result *= factor;
        }
        rest >>= 1U;
        if (rest != 0) {
            factor *= factor;
        }
    }
    if (negative) {
        result = std::complex<T>{1, 0} / result;
    }
    return result;
}

} // namespace detail

/// `z ^ w` (:op/pow_c, :op/pow_z): an integer exponent is repeated squaring, every other
/// exponent is std::pow of the two parts (the runtime's complexPower).
inline std::complex<float> pow(std::complex<float> base, std::complex<float> exponent) {
    const float real = exponent.imag() == 0.0f ? exponent.real()
                                               : std::numeric_limits<float>::quiet_NaN();
    if (std::isfinite(real) && std::trunc(real) == real && real >= -9007199254740992.0f &&
        real <= 9007199254740992.0f) {
        return detail::integerPower(base, static_cast<long long>(real));
    }
    return std::pow(base, exponent);
}
inline std::complex<double> pow(std::complex<double> base, std::complex<double> exponent) {
    const double real = exponent.imag() == 0.0 ? exponent.real()
                                               : std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(real) && std::trunc(real) == real && real >= -9007199254740992.0 &&
        real <= 9007199254740992.0) {
        return detail::integerPower(base, static_cast<long long>(real));
    }
    return std::pow(base, exponent);
}

/// `z ^ n` with an integer n (:op/pow_c_i, :op/pow_c_l, ...): the repeated squaring above.
inline std::complex<float> pow(std::complex<float> base, int64_t exponent) {
    return detail::integerPower(base, static_cast<long long>(exponent));
}
inline std::complex<double> pow(std::complex<double> base, int64_t exponent) {
    return detail::integerPower(base, static_cast<long long>(exponent));
}

/// A number as a complex value with a zero imaginary part (`c64(x)`, `c128(x)`).
template <typename To, typename From> std::complex<To> complexFromReal(From value) {
    return std::complex<To>{static_cast<To>(value), To{0}};
}

/// A complex value in the other width (`c64(z)`, `c128(z)`): each part converted.
template <typename To, typename From>
std::complex<To> complexFromComplex(const std::complex<From> &value) {
    return std::complex<To>{static_cast<To>(value.real()), static_cast<To>(value.imag())};
}

/// `math:re_c` / `math:re_z` and `math:im_c` / `math:im_z`.
inline float complexReal(std::complex<float> value) { return value.real(); }
inline double complexReal(std::complex<double> value) { return value.real(); }
inline float complexImag(std::complex<float> value) { return value.imag(); }
inline double complexImag(std::complex<double> value) { return value.imag(); }

/// `math:abs_c` / `math:abs_z`: |z| = hypot(a, b) in the width of the parts, as the module's
/// __math_abs__ computes it.
inline float complexAbs(std::complex<float> value) {
    return std::hypot(value.real(), value.imag());
}
inline double complexAbs(std::complex<double> value) {
    return std::hypot(value.real(), value.imag());
}

} // namespace camel::standalone