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

/*
 * The conversions between numbers, and from text to a number, that can fail when they run
 * (design principles 5.1, "数值"). The conversion functions (`int(x)`, `i32(x)`, `float(s)`,
 * ...) and the `as` conversion (Type::castSlotFrom) both come here, so that the two spellings
 * and the four back ends fault on the same values with the same diagnostic.
 *
 *   float -> integer    NaN, an infinity, or a value whose truncation is outside the integer
 *                       type: RuntimeDiag::ConversionOutOfRange.
 *   int -> i32          a value outside i32: ConversionOutOfRange.
 *   string -> number    the whole string must be one number (design principles 5.1):
 *                       RuntimeDiag::InvalidNumberText when it is not one, and
 *                       ConversionOutOfRange when it is one the type cannot hold (an overflow
 *                       or an underflow). A good number that does not fit is not "not a
 *                       number"; the message quotes it and gives the range.
 *
 * Not faults: float -> f32 beyond the range of f32 gives an infinity (IEEE rounding, like any
 * float arithmetic that overflows), and an integer too large to be exact as a float is rounded.
 */

#pragma once

#include "camel/core/error/runtime.h"
#include "camel/utils/number_text.h"

#include <concepts>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace camel::core {

/// Why a value does not fit the integer type To (`target` is its name in a message): the
/// reason part of ConversionOutOfRange's message.
template <std::signed_integral To> std::string integerRangeReason(const char *target) {
    return std::format(
        "the value is outside the range of {} ({})", target, camel::utils::integerRangeText<To>());
}

/// Why a number does not fit the floating-point type To (`target` is its name in a message).
template <std::floating_point To> std::string floatRangeReason(const char *target) {
    return std::format(
        "the value is outside the range of {} ({})", target, camel::utils::floatRangeText<To>());
}

/// Why a nonzero number is too small for the floating-point type To (`target` is its name in a
/// message): it rounds to zero, and a value that is not zero is never read as one (H15i). The
/// same sentence names a part of a complex number ("the imaginary part is too small for c64").
inline std::string floatUnderflowReason(const char *target, std::string_view part = {}) {
    return part.empty()
               ? std::format("the value is too small for {} (the nearest value is zero)", target)
               : std::format(
                     "the {} part is too small for {} (the nearest value is zero)", part, target);
}

/// `value` truncated toward zero as the integer type To (`target` is its name in a message).
template <std::signed_integral To, std::floating_point From>
To checkedTruncate(From value, const char *target) {
    if (!camel::utils::truncatesInto<To, From>(value)) [[unlikely]] {
        error::throwRuntimeFault(
            error::RuntimeDiag::ConversionOutOfRange,
            camel::utils::floatText(value),
            target,
            value != value
                ? std::string("NaN is not a number an integer can hold")
                : integerRangeReason<To>(target));
    }
    return static_cast<To>(value);
}

/// `value` as the narrower integer type To.
template <std::signed_integral To, std::signed_integral From>
To checkedNarrow(From value, const char *target) {
    if (value < static_cast<From>(std::numeric_limits<To>::min()) ||
        value > static_cast<From>(std::numeric_limits<To>::max())) [[unlikely]] {
        error::throwRuntimeFault(
            error::RuntimeDiag::ConversionOutOfRange,
            value,
            target,
            integerRangeReason<To>(target));
    }
    return static_cast<To>(value);
}

/// The integer written in `text`, which must be that integer and nothing else. A text that is
/// not one integer is InvalidNumberText; one that is an integer outside the range of To is
/// ConversionOutOfRange, with the range in the message.
template <std::signed_integral To> To integerFromText(std::string_view text, const char *target) {
    To value{};
    switch (camel::utils::parseWholeInteger<To>(text, value)) {
    case camel::utils::NumberTextStatus::Ok:
        return value;
    case camel::utils::NumberTextStatus::OutOfRange:
        error::throwRuntimeFault(
            error::RuntimeDiag::ConversionOutOfRange,
            "'" + std::string(text) + "'",
            target,
            integerRangeReason<To>(target));
    default:
        error::throwRuntimeFault(
            error::RuntimeDiag::InvalidNumberText,
            std::string(text),
            target,
            "an integer");
    }
}

/// The number written in `text`, which must be that number and nothing else. A text that is not
/// one number is InvalidNumberText; one the type cannot hold (an overflow or an underflow) is
/// ConversionOutOfRange, with the range in the message.
template <std::floating_point To> To floatFromText(std::string_view text, const char *target) {
    To value{};
    switch (camel::utils::parseWholeFloat<To>(text, value)) {
    case camel::utils::NumberTextStatus::Ok:
        return value;
    case camel::utils::NumberTextStatus::OutOfRange:
        error::throwRuntimeFault(
            error::RuntimeDiag::ConversionOutOfRange,
            "'" + std::string(text) + "'",
            target,
            floatRangeReason<To>(target));
    default:
        error::throwRuntimeFault(
            error::RuntimeDiag::InvalidNumberText,
            std::string(text),
            target,
            "a number");
    }
}

} // namespace camel::core
