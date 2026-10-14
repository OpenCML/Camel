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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Numbers in the programs std::cpp writes: the arithmetic of the language and the conversions
 * between numbers (design principles 2.10, "数值").
 *
 * Each function does what the operator of the runtime with the same name does (the URI is
 * given with it; src/builtin/operators/ops.cpp and cast.cpp), including where that is a fault:
 *
 *   - integer +, -, *, unary - wrap around (C++ signed overflow is undefined; these go through
 *     the unsigned type and compile to the same instructions);
 *   - / and % by zero are faults, for floats too; the smallest integer / -1 wraps around, and
 *     any % -1 is 0 (camel/utils/int_div.h, the same code as the runtime's);
 *   - a conversion whose value does not fit the target type is a fault, and text that is not
 *     exactly one number is a fault (camel/utils/number_text.h, the same code again).
 *
 * The functions that can fault take the Site of the operation; its report says what the values
 * passed to fault() are (core.h).
 */

#pragma once

#include "camel/standalone/core.h"
#include "camel/utils/int_div.h"
#include "camel/utils/number_text.h"

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace camel::standalone {

// ---- Arithmetic (:op/add_l, :op/sub_l, :op/mul_l, :op/neg_l and the _i, _f, _d forms) ----
//
// The int64 and double forms are plain functions and the int32 and float forms are templates
// that only accept exactly that type. So an operand of the generated code next to a bare
// literal (`rt::sub(n, 1)`, the literal being an `int`) selects the form of the operand: the
// template cannot deduce one type from `int64_t` and `int`, and the plain function converts
// the literal.

namespace detail {

template <std::signed_integral T> constexpr T wrapAdd(T a, T b) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) + static_cast<U>(b));
}

template <std::signed_integral T> constexpr T wrapSub(T a, T b) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) - static_cast<U>(b));
}

template <std::signed_integral T> constexpr T wrapMul(T a, T b) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(a) * static_cast<U>(b));
}

template <std::signed_integral T> constexpr T wrapNeg(T a) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(U{0} - static_cast<U>(a));
}

template <typename T> T divide(T a, T b, const Site &site) {
    if (b == T{0}) [[unlikely]] {
        fault(site);
    }
    return camel::utils::quotient(a, b);
}

template <typename T> T remainder(T a, T b, const Site &site) {
    if (b == T{0}) [[unlikely]] {
        fault(site);
    }
    return camel::utils::remainder(a, b);
}

} // namespace detail

/// Integer `+`, `-`, `*` and unary `-`: they wrap around (design principles 2.10).
constexpr int64_t add(int64_t a, int64_t b) { return detail::wrapAdd(a, b); }
constexpr int64_t sub(int64_t a, int64_t b) { return detail::wrapSub(a, b); }
constexpr int64_t mul(int64_t a, int64_t b) { return detail::wrapMul(a, b); }
constexpr int64_t neg(int64_t a) { return detail::wrapNeg(a); }
template <std::same_as<int32_t> T> constexpr T add(T a, T b) { return detail::wrapAdd(a, b); }
template <std::same_as<int32_t> T> constexpr T sub(T a, T b) { return detail::wrapSub(a, b); }
template <std::same_as<int32_t> T> constexpr T mul(T a, T b) { return detail::wrapMul(a, b); }
template <std::same_as<int32_t> T> constexpr T neg(T a) { return detail::wrapNeg(a); }

/// `a / b` (:op/div_*): dividing by zero is DivisionByZero at `site`, for floats too; the
/// smallest integer divided by -1 wraps around.
inline int64_t div(int64_t a, int64_t b, const Site &site) { return detail::divide(a, b, site); }
inline double div(double a, double b, const Site &site) { return detail::divide(a, b, site); }
template <std::same_as<int32_t> T> T div(T a, T b, const Site &site) {
    return detail::divide(a, b, site);
}
template <std::same_as<float> T> T div(T a, T b, const Site &site) {
    return detail::divide(a, b, site);
}

/// `a % b` (:op/mod_*): integers only; by zero is DivisionByZero at `site`; any % -1 is 0.
inline int64_t mod(int64_t a, int64_t b, const Site &site) {
    return detail::remainder(a, b, site);
}
template <std::same_as<int32_t> T> T mod(T a, T b, const Site &site) {
    return detail::remainder(a, b, site);
}

/// `a ^ b` (:op/pow_*): what the runtime computes, std::pow converted back to the type.
inline int64_t pow(int64_t base, int64_t exponent) {
    return static_cast<int64_t>(std::pow(base, exponent));
}
inline double pow(double base, double exponent) { return std::pow(base, exponent); }
template <std::same_as<int32_t> T> T pow(T base, T exponent) {
    return static_cast<T>(std::pow(base, exponent));
}
template <std::same_as<float> T> T pow(T base, T exponent) {
    return static_cast<T>(std::pow(base, exponent));
}

// ---- Number to number (:op/ltoi, :op/dtol, ...) ----

namespace detail {

template <std::signed_integral T> std::string rangeText(const char *target) {
    return std::string("the value is outside the range of ") + target + " (" +
           std::to_string(std::numeric_limits<T>::min()) + " to " +
           std::to_string(std::numeric_limits<T>::max()) + ")";
}

} // namespace detail

/// An integer as the narrower integer type To (`i32(x)`); a value outside it is
/// ConversionOutOfRange at `site`, with the value, the target's name and why (values 0, 1, 2).
template <std::signed_integral To, std::signed_integral From>
To narrow(From value, const char *target, const Site &site) {
    if (value < static_cast<From>(std::numeric_limits<To>::min()) ||
        value > static_cast<From>(std::numeric_limits<To>::max())) [[unlikely]] {
        fault(site, {std::to_string(value), target, detail::rangeText<To>(target)});
    }
    return static_cast<To>(value);
}

/// A float truncated toward zero as the integer type To (`int(x)`); NaN, an infinity or a
/// value outside To is ConversionOutOfRange at `site` (values as for narrow()).
template <std::signed_integral To, std::floating_point From>
To truncate(From value, const char *target, const Site &site) {
    if (!camel::utils::truncatesInto<To, From>(value)) [[unlikely]] {
        fault(
            site,
            {camel::utils::floatText(value),
             target,
             value != value ? std::string("NaN is not a number an integer can hold")
                            : detail::rangeText<To>(target)});
    }
    return static_cast<To>(value);
}

/// The integer written in `text`, which must be that integer and nothing else (`int(s)`);
/// anything else is InvalidNumberText at `site`, with the text, what it was to become and
/// what it must be (values 0, 1, 2).
template <std::signed_integral To>
To integerFromText(std::string_view text, const char *target, const Site &site) {
    To value{};
    switch (camel::utils::parseWholeInteger<To>(text, value)) {
    case camel::utils::NumberTextStatus::Ok:
        return value;
    case camel::utils::NumberTextStatus::OutOfRange:
        fault(
            site,
            {std::string(text),
             std::string(target) + " (the number is outside its range)",
             "an integer"});
    default:
        fault(site, {std::string(text), target, "an integer"});
    }
}

/// The number written in `text` (`float(s)`), under the same rule; values as above.
template <std::floating_point To>
To floatFromText(std::string_view text, const char *target, const Site &site) {
    To value{};
    switch (camel::utils::parseWholeFloat<To>(text, value)) {
    case camel::utils::NumberTextStatus::Ok:
        return value;
    case camel::utils::NumberTextStatus::OutOfRange:
        fault(
            site,
            {std::string(text),
             std::string(target) + " (the number is outside its range)",
             "a number"});
    default:
        fault(site, {std::string(text), target, "a number"});
    }
}

} // namespace camel::standalone
