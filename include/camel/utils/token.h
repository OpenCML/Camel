// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace camel_number_detail {

// Whether the standard library converts text to T with std::from_chars. libstdc++ (11 and
// later) and the MSVC library do; libc++ does from version 20 on.
template <typename T>
concept HasFloatFromChars = requires(const char *p, T &value) { std::from_chars(p, p, value); };

// The value of the decimal numeral `text[begin..]`, written as digits with an optional
// fraction and an optional exponent (`12`, `0.7747`, `1.5e-3`), without a sign.
//
// The result is the correctly rounded one: the T nearest to the number written, ties to even.
// It is found by the standard library, not by arithmetic of this file. Summing the digits
// times powers of ten rounds at every step, so the last bit depended on the literal, and on
// whether the compiler fused a multiply and an add (0.7747 read as 0.77470000000000017 in a
// build with FMA and as 0.77470000000000006 without; the nearest double is neither).
template <typename T> T parseDecimalReal(const std::string &text, size_t begin) {
    // Check the shape first: from_chars and strtod take more than a Camel literal (`inf`,
    // `nan`, hexadecimal floats).
    size_t i             = begin;
    bool anyDigit        = false;
    bool anyNonZero      = false;
    bool seenPoint       = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.' && !seenPoint) {
            seenPoint = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            anyDigit = true;
            if (!anyNonZero && c != '0') {
                anyNonZero = true;
            }
        } else {
            break;
        }
    }
    if (!anyDigit) {
        throw std::invalid_argument("Invalid number format: " + text);
    }
    if (i < text.size()) {
        if (text[i] != 'e' && text[i] != 'E') {
            throw std::invalid_argument("Invalid number format: " + text);
        }
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        if (i == text.size()) {
            throw std::invalid_argument("Invalid number format: " + text);
        }
        for (; i < text.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
                throw std::invalid_argument("Invalid number format: " + text);
            }
        }
    }

    T value          = 0;
    bool outOfRange  = false;
    const char *from = text.data() + begin;
    const char *to   = text.data() + text.size();
    if constexpr (HasFloatFromChars<T>) {
        const auto result = std::from_chars(from, to, value);
        outOfRange        = result.ec == std::errc::result_out_of_range;
        if (!outOfRange && (result.ec != std::errc() || result.ptr != to)) {
            throw std::invalid_argument("Invalid number format: " + text);
        }
    } else {
        // The C library's conversion, which rounds correctly on the platforms that come here.
        // It reads the decimal point of the C locale, which Camel never changes from ".".
        const std::string numeral(from, to);
        char *end = nullptr;
        errno     = 0;
        value     = static_cast<T>(
            sizeof(T) <= sizeof(float) ? std::strtof(numeral.c_str(), &end)
                                       : std::strtod(numeral.c_str(), &end));
        outOfRange = errno == ERANGE && (value == 0 || std::isinf(value));
        if (end != numeral.c_str() + numeral.size()) {
            throw std::invalid_argument("Invalid number format: " + text);
        }
    }
    if (outOfRange) {
        // A literal too large for T, or a nonzero one too small to tell from zero, is an error
        // a pass parameter is a literal too, and no reader turns a nonzero text into
        // zero.
        if (anyNonZero) {
            throw std::overflow_error("Number out of range");
        }
        value = 0;
    }
    return value;
}

} // namespace camel_number_detail

// An integer literal as the lexer accepts it: an optional sign, then decimal digits with an
// optional exponent (`12`, `1e6`, `15e-1`), or `0x` / `0b` / `0o` (octal) and digits. The
// arithmetic is exact. A negative exponent divides and drops the fraction (`15e-1` is 1), which
// `inexact` records. `-9223372036854775808` is in range although its digits alone are not. A
// leading-zero decimal is not a source token: the lexer refuses it and octal is `0o77`.
enum class IntegerLiteralStatus { Ok, OutOfRange, Invalid };

struct IntegerLiteral {
    IntegerLiteralStatus status = IntegerLiteralStatus::Ok;
    int64_t value               = 0;
    bool inexact                = false;
};

inline IntegerLiteral parseIntegerLiteral(const std::string &input) {
    IntegerLiteral out;
    size_t i        = 0;
    bool isNegative = false;
    if (i < input.size() && (input[i] == '-' || input[i] == '+')) {
        isNegative = input[i] == '-';
        ++i;
    }
    unsigned base = 10;
    if (input.compare(i, 2, "0x") == 0) {
        base = 16;
        i += 2;
    } else if (input.compare(i, 2, "0b") == 0) {
        base = 2;
        i += 2;
    } else if (input.compare(i, 2, "0o") == 0) {
        base = 8;
        i += 2;
    }
    // The magnitude is at most 2^63 (the smallest int64 negated).
    const uint64_t limit = isNegative ? (uint64_t{1} << 63) : (uint64_t{1} << 63) - 1;
    uint64_t magnitude   = 0;
    bool overflow        = false;
    bool anyDigit        = false;
    for (; i < input.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        unsigned digit        = 0;
        if (std::isdigit(c)) {
            digit = c - '0';
        } else if (base == 16 && std::isxdigit(c)) {
            digit = static_cast<unsigned>(std::tolower(c) - 'a') + 10;
        } else {
            break;
        }
        if (digit >= base) {
            out.status = IntegerLiteralStatus::Invalid;
            return out;
        }
        anyDigit = true;
        if (overflow || magnitude > (limit - digit) / base) {
            overflow = true;
        } else {
            magnitude = magnitude * base + digit;
        }
    }
    if (!anyDigit) {
        out.status = IntegerLiteralStatus::Invalid;
        return out;
    }
    if (i < input.size()) {
        if (base != 10 || (input[i] != 'e' && input[i] != 'E')) {
            out.status = IntegerLiteralStatus::Invalid;
            return out;
        }
        ++i;
        bool negativeExponent = false;
        if (i < input.size() && (input[i] == '+' || input[i] == '-')) {
            negativeExponent = input[i] == '-';
            ++i;
        }
        if (i == input.size()) {
            out.status = IntegerLiteralStatus::Invalid;
            return out;
        }
        unsigned long exponent = 0;
        for (; i < input.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(input[i]))) {
                out.status = IntegerLiteralStatus::Invalid;
                return out;
            }
            if (exponent < 100000) {
                exponent = exponent * 10 + static_cast<unsigned long>(input[i] - '0');
            }
        }
        if (negativeExponent) {
            // The digits were too many for an int64 only before the division.
            if (overflow) {
                out.status = IntegerLiteralStatus::OutOfRange;
                return out;
            }
            for (unsigned long k = 0; k < exponent && magnitude != 0; ++k) {
                out.inexact = out.inexact || magnitude % 10 != 0;
                magnitude /= 10;
            }
        } else {
            for (unsigned long k = 0; k < exponent && magnitude != 0 && !overflow; ++k) {
                if (magnitude > limit / 10) {
                    overflow = true;
                } else {
                    magnitude *= 10;
                }
            }
        }
    }
    if (overflow) {
        out.status = IntegerLiteralStatus::OutOfRange;
        return out;
    }
    // Negation in unsigned arithmetic: 2^63 becomes the smallest int64 without overflow.
    out.value = static_cast<int64_t>(isNegative ? uint64_t{0} - magnitude : magnitude);
    return out;
}

template <typename T> T parseNumber(const std::string &input) {
    static_assert(
        std::is_integral_v<T> || std::is_floating_point_v<T>,
        "parseNumber only supports integral and floating point types");

    bool isNegative = false;
    T result        = 0;
    int base        = 10;
    int exponent    = 0;
    size_t i        = 0;

    // Handle sign
    if (input[0] == '-') {
        isNegative = true;
        i++;
    } else if (input[0] == '+') {
        i++;
    }

    // Handle different bases
    if (input.substr(i, 2) == "0x") {
        base = 16;
        i += 2;
    } else if (input.substr(i, 2) == "0b") {
        base = 2;
        i += 2;
    } else if (
        input[i] == '0' && input.find_first_of(".") == std::string::npos &&
        (std::is_integral_v<T> || input.find_first_of("eE") == std::string::npos)) {
        // A leading zero makes an octal integer; `0e5` is the floating-point zero.
        base = 8;
        i++;
    }

    if constexpr (std::is_floating_point_v<T>) {
        if (base == 10) {
            const T magnitude = camel_number_detail::parseDecimalReal<T>(input, i);
            return isNegative ? -magnitude : magnitude;
        }
    }

    // Parse the number part
    while (i < input.length()) {
        if (base == 10 && input[i] == '.') {
            // For integer types, discard the fractional part
            if constexpr (std::is_integral_v<T>) {
                // Skip the fractional part
                while (i < input.length() && isdigit(input[i])) {
                    i++;
                }
                break;
            }
            // Parse floating point
            i++;
            double fractional = 0.0;
            double weight     = 0.1;
            while (i < input.length() && isdigit(input[i])) {
                fractional += (input[i] - '0') * weight;
                weight *= 0.1;
                i++;
            }
            result += fractional;
        } else if (base == 10 && (input[i] == 'e' || input[i] == 'E')) {
            // Parse exponent part
            i++;
            bool isNegativeExponent = false;
            if (input[i] == '-') {
                isNegativeExponent = true;
                i++;
            } else if (input[i] == '+') {
                i++;
            }
            while (i < input.length() && isdigit(input[i])) {
                exponent = exponent * 10 + (input[i] - '0');
                i++;
            }
            if (isNegativeExponent) {
                exponent = -exponent;
            }
        } else if (isdigit(input[i])) {
            // Parse integer part
            result = result * base + (input[i] - '0');
            i++;
        } else if (base == 16 && isxdigit(input[i])) {
            // Parse hexadecimal
            result = result * 16 + (toupper(input[i]) - (isdigit(input[i]) ? '0' : 'A' - 10));
            i++;
        } else if (base == 2 && (input[i] == '0' || input[i] == '1')) {
            // Parse binary
            result = result * 2 + (input[i] - '0');
            i++;
        } else if (base == 8 && (input[i] >= '0' && input[i] <= '7')) {
            // Parse octal
            result = result * 8 + (input[i] - '0');
            i++;
        } else {
            // Encountered an invalid character
            throw std::invalid_argument("Invalid number format: " + input);
        }
    }

    // Apply exponent. Only when there is one: the product goes through double, which cannot
    // hold an integer above 2^53 (9223372036854775807 came back as the smallest int64).
    if (exponent != 0) {
        result *= std::pow(10.0, exponent);
    }

    // Handle sign
    if (isNegative) {
        result = -result;
    }

    // Check for overflow/underflow
    if (result > std::numeric_limits<T>::max() || result < std::numeric_limits<T>::lowest()) {
        throw std::overflow_error("Number out of range");
    }

    return result;
}
