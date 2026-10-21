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
 * Numbers and text, and numbers between types: the rules of design principles 5.1 ("数值"),
 * in one place so that every conversion function, every back end and every printer applies the
 * same ones.
 *
 *   text -> number   the whole string must be one number: no blanks, nothing after it.
 *   float -> text    one rule: the shortest text that reads back as the same number.
 *   number -> number a value that does not fit the target type is a fault
 *                    (camel/core/numeric_convert.h raises it; here are the range tests).
 */

#pragma once

#include <cerrno>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

namespace camel::utils {

enum class NumberTextStatus { Ok, NotANumber, OutOfRange };

/// Reads `text` as one integer of type T: an optional sign ('-' or '+'), then decimal digits,
/// and nothing else (no blanks, no digit separators, no base prefix).
template <std::signed_integral T> NumberTextStatus parseWholeInteger(std::string_view text, T &out) {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (!text.empty() && text.front() == '-') {
            return NumberTextStatus::NotANumber;
        }
    }
    if (text.empty()) {
        return NumberTextStatus::NotANumber;
    }
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (ec == std::errc::invalid_argument) {
        return NumberTextStatus::NotANumber;
    }
    if (ec == std::errc::result_out_of_range) {
        // from_chars stops after the digits it could read; what follows decides whether the
        // text was a number at all.
        for (const char *p = end; p != text.data() + text.size(); ++p) {
            if (*p < '0' || *p > '9') {
                return NumberTextStatus::NotANumber;
            }
        }
        return NumberTextStatus::OutOfRange;
    }
    if (end != text.data() + text.size()) {
        return NumberTextStatus::NotANumber;
    }
    out = value;
    return NumberTextStatus::Ok;
}

/// Reads `text` as one floating-point number of type T: an optional sign, then a decimal
/// number with an optional fraction and exponent (`1`, `1.5`, `.5`, `1e-3`), or `inf` / `nan`
/// as the language prints them. Nothing else: no blanks, no hexadecimal form, no suffix.
template <std::floating_point T> NumberTextStatus parseWholeFloat(std::string_view text, T &out) {
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return NumberTextStatus::NotANumber;
    }
    if (text == "inf") {
        out = negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
        return NumberTextStatus::Ok;
    }
    if (text == "nan") {
        out = std::numeric_limits<T>::quiet_NaN();
        return NumberTextStatus::Ok;
    }
    // Only a digit or a point may start the number (from_chars would also take "infinity",
    // "nan(...)" and a second sign).
    if (!(text.front() >= '0' && text.front() <= '9') && text.front() != '.') {
        return NumberTextStatus::NotANumber;
    }
    T value{};
    if constexpr (requires(const char *p, T &v) { std::from_chars(p, p, v); }) {
        const auto [end, ec] = std::from_chars(
            text.data(),
            text.data() + text.size(),
            value,
            std::chars_format::general);
        if (ec == std::errc::invalid_argument || end != text.data() + text.size()) {
            return NumberTextStatus::NotANumber;
        }
        if (ec == std::errc::result_out_of_range) {
            return NumberTextStatus::OutOfRange;
        }
    } else {
        // libc++ before version 20 has no from_chars for floating point. The C library's
        // conversion takes more than a decimal number (a hexadecimal form), so the characters
        // are checked first; it reads the decimal point of the C locale, which Camel never
        // changes from ".".
        for (const char c : text) {
            const bool allowed =
                (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
            if (!allowed) {
                return NumberTextStatus::NotANumber;
            }
        }
        const std::string numeral(text);
        char *end = nullptr;
        errno     = 0;
        value     = static_cast<T>(
            sizeof(T) <= sizeof(float) ? std::strtof(numeral.c_str(), &end)
                                       : std::strtod(numeral.c_str(), &end));
        if (end != numeral.c_str() + numeral.size()) {
            return NumberTextStatus::NotANumber;
        }
        if (errno == ERANGE && std::isinf(value)) {
            return NumberTextStatus::OutOfRange;
        }
    }
    out = negative ? -value : value;
    return NumberTextStatus::Ok;
}

/// The text of a floating-point number: the shortest digits that read back as the same number
/// (design principles 7.7), laid out so that the text is always recognisable as a float.
///
///   - a number whose decimal exponent is from -4 to 15 is written without an exponent, with
///     at least one digit after the point: `2.0`, `100000.0`, `0.1`, `0.0001`;
///   - any other is written with one: `1e+16`, `1.5e-07` (a sign and at least two digits);
///   - the zeros are `0.0` and `-0.0`, the infinities `inf` and `-inf`;
///   - every NaN is `nan`, never `-nan`: the sign of a NaN says nothing about a value.
///
/// The text of a `f32` is the shortest that reads back as that `f32` (`0.1`, not
/// `0.10000000149011612`). parseWholeFloat reads every text this writes.
template <std::floating_point T> std::string floatText(T value) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    // Shortest round-trip digits in scientific form: [-]d[.ddd]e[+-]dd
    char buffer[64];
    const auto [end, ec] =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    if (ec != std::errc()) {
        return "nan";
    }
    std::string_view sci(buffer, static_cast<size_t>(end - buffer));
    const bool negative = !sci.empty() && sci.front() == '-';
    if (negative) {
        sci.remove_prefix(1);
    }
    const size_t ePos = sci.find('e');
    std::string digits;
    for (const char c : sci.substr(0, ePos)) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    int exponent = 0;
    {
        std::string_view expText = sci.substr(ePos + 1);
        if (!expText.empty() && expText.front() == '+') {
            expText.remove_prefix(1);
        }
        std::from_chars(expText.data(), expText.data() + expText.size(), exponent);
    }

    std::string text = negative ? "-" : "";
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            text += "0.";
            text.append(static_cast<size_t>(-exponent - 1), '0');
            text += digits;
        } else {
            const size_t whole = static_cast<size_t>(exponent) + 1;
            if (digits.size() <= whole) {
                text += digits;
                text.append(whole - digits.size(), '0');
                text += ".0";
            } else {
                text.append(digits, 0, whole);
                text += '.';
                text.append(digits, whole, std::string::npos);
            }
        }
        return text;
    }
    text += digits.front();
    if (digits.size() > 1) {
        text += '.';
        text.append(digits, 1, std::string::npos);
    }
    text += 'e';
    text += exponent < 0 ? '-' : '+';
    const int magnitude = exponent < 0 ? -exponent : exponent;
    if (magnitude < 10) {
        text += '0';
    }
    text += std::to_string(magnitude);
    return text;
}

/// The value to hand to a text formatter: a NaN becomes the standard quiet NaN, which has no
/// sign bit set. A NaN's sign says nothing about a value, and text never shows it (`floatText`;
/// the language reference, 2.5.2: "a NaN has no sign"). Every path that formats a number through
/// a library (the interpreter hands floats to fmt) applies this first, so no path can write
/// `-nan`, with or without a format specification.
template <std::floating_point T> T withoutNaNSign(T value) {
    return std::isnan(value) ? std::numeric_limits<T>::quiet_NaN() : value;
}

/// The text of one part of a complex number: a whole part is written as an integer (`3`, so
/// that `3+4i` is the text of the value, as design principles 5.1 writes it), every other part
/// by floatText. `-0` is written `0` as a part: the sign of a zero part is not shown.
template <std::floating_point T> std::string complexPartText(T value) {
    if (std::isfinite(value) && value == std::trunc(value) && std::abs(value) < T(1e16)) {
        char buffer[32];
        const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
        if (ec == std::errc()) {
            return std::string(buffer, static_cast<size_t>(end - buffer));
        }
    }
    if (value == 0) {
        return "0";
    }
    return floatText(value);
}

/// The text of a complex number: the real part, the sign of the imaginary one, its magnitude
/// and `i` (`3+4i`, `3-4i`, `0+1i`). Both parts are always written, so a real-looking complex
/// value still reads as a complex one.
template <std::floating_point T> std::string complexText(T real, T imaginary) {
    std::string text = complexPartText(real);
    const bool negative = !std::isnan(imaginary) && std::signbit(imaginary);
    text += negative ? '-' : '+';
    text += complexPartText(negative ? -imaginary : imaginary);
    text += 'i';
    return text;
}

/// The two parts of the text of a complex number, each still to be read as a number. `real` is
/// empty when the text holds only an imaginary part (`-2i`, `4.5i`).
struct ComplexTextParts {
    std::string_view real;
    std::string_view imaginary;
};

/// Splits the text of a complex number as the language writes one — the real part, the sign of
/// the imaginary part and the imaginary part (`3+4i`, `3-4i`, `0.5-0.25i`), or only the
/// imaginary part with its sign (`4i`, `-2i`) — into the two part texts. Nothing is read here:
/// the caller reads each part by the rule of the number type, so a complex value and a real one
/// are read by the same rule. Returns false when `text` is not a complex number: it must end in
/// `i`, and its imaginary part must not be empty (`1+` is not one). A sign that belongs to an
/// exponent (`1e-3i`, `-1e-3i`) is not a separator.
inline bool splitComplexText(std::string_view text, ComplexTextParts &out) {
    if (text.size() < 2 || text.back() != 'i') {
        return false;
    }
    text.remove_suffix(1);
    if (text.empty()) {
        return false;
    }
    // The imaginary part begins at its sign: the last '+' or '-' that is not the number's own
    // leading sign and not the sign of an exponent. Without one the text is only the imaginary
    // part (`2i`, `-2i`).
    std::size_t at = std::string_view::npos;
    for (std::size_t i = text.size(); i-- > 1;) {
        const char c = text[i];
        if ((c == '+' || c == '-') && text[i - 1] != 'e' && text[i - 1] != 'E') {
            at = i;
            break;
        }
    }
    if (at == std::string_view::npos) {
        out.real      = {};
        out.imaginary = text;
        return true;
    }
    if (at + 1 >= text.size()) {
        return false;
    }
    out.real      = text.substr(0, at);
    out.imaginary = text.substr(at);
    return true;
}

/// Whether `value`, truncated toward zero, is a value of the signed integer type T. False for
/// a NaN and for the infinities.
template <std::signed_integral T, std::floating_point F> bool truncatesInto(F value) {
    if (!std::isfinite(value)) {
        return false;
    }
    // Both bounds are powers of two, exact in every floating-point type: -(2^(N-1)) is the
    // smallest value that fits, 2^(N-1) the smallest that does not.
    const F truncated = std::trunc(value);
    const F lower     = static_cast<F>(std::numeric_limits<T>::min());
    return truncated >= lower && truncated < -lower;
}

} // namespace camel::utils
