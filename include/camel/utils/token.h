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
 * Created: May. 04, 2024
 * Updated: Apr. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
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
    // Position of the first non-zero digit against the decimal point: 3 for `123.4`, -2 for
    // `0.004`. Only its sign is used, to tell a number too large from one too small.
    long long magnitude  = 0;
    bool seenPoint       = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.' && !seenPoint) {
            seenPoint = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            anyDigit = true;
            if (!anyNonZero) {
                if (c != '0') {
                    anyNonZero = true;
                    magnitude += seenPoint ? 0 : 1;
                } else if (seenPoint) {
                    --magnitude;
                }
            } else if (!seenPoint) {
                ++magnitude;
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
        bool negativeExponent = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            negativeExponent = text[i] == '-';
            ++i;
        }
        if (i == text.size()) {
            throw std::invalid_argument("Invalid number format: " + text);
        }
        long long exponent = 0;
        for (; i < text.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
                throw std::invalid_argument("Invalid number format: " + text);
            }
            if (exponent < 1000000) {
                exponent = exponent * 10 + (text[i] - '0');
            }
        }
        magnitude += negativeExponent ? -exponent : exponent;
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
        // As before: a literal too large for T is an error, one too small to tell from zero
        // is zero.
        if (anyNonZero && magnitude > 0) {
            throw std::overflow_error("Number out of range");
        }
        value = 0;
    }
    return value;
}

} // namespace camel_number_detail

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
    } else if (input[i] == '0' && input.find_first_of(".") == std::string::npos) {
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
