/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 4, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

template <typename T> T parseNumber(const std::string &input) {
    bool isNegative = false;
    T result = 0;
    int base = 10;
    int exponent = 0;
    size_t i = 0;

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
            double weight = 0.1;
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

    // Apply exponent
    result *= std::pow(10.0, exponent);

    // Handle sign
    if (isNegative) {
        result = -result;
    }

    // Check for overflow/underflow
    if (result > std::numeric_limits<T>::max() || result < std::numeric_limits<T>::min()) {
        throw std::overflow_error("Number out of range");
    }

    return result;
}