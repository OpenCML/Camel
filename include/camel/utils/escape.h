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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Sep. 27, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "camel/utils/utf8.h"

inline std::string encodeEscapes(const std::string &input) {
    std::ostringstream oss;

    for (unsigned char c : input) {
        switch (c) {
        case '\n':
            oss << "\\n";
            break;
        case '\t':
            oss << "\\t";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\a':
            oss << "\\a";
            break;
        case '\b':
            oss << "\\b";
            break;
        case '\f':
            oss << "\\f";
            break;
        case '\v':
            oss << "\\v";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\"':
            oss << "\\\"";
            break;
        case '\'':
            oss << "\\\'";
            break;
        case '\0':
            oss << "\\0";
            break;
        default:
            if (std::isprint(c)) {
                oss << c;
            } else {
                // Non-printable characters are emitted as hexadecimal \xHH.
                oss << "\\x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << (int)c;
            }
        }
    }

    return oss.str();
}

inline std::string decodeEscapes(const std::string &input) {
    std::ostringstream oss;
    size_t i   = 0;
    size_t len = input.length();

    while (i < len) {
        char c = input[i];
        if (c == '\\' && i + 1 < len) {
            char next = input[i + 1];
            switch (next) {
            case 'n':
                oss << '\n';
                i += 2;
                break;
            case 't':
                oss << '\t';
                i += 2;
                break;
            case 'r':
                oss << '\r';
                i += 2;
                break;
            case 'a':
                oss << '\a';
                i += 2;
                break;
            case 'b':
                oss << '\b';
                i += 2;
                break;
            case 'f':
                oss << '\f';
                i += 2;
                break;
            case 'v':
                oss << '\v';
                i += 2;
                break;
            case '\\':
                oss << '\\';
                i += 2;
                break;
            case '\"':
                oss << '\"';
                i += 2;
                break;
            case '\'':
                oss << '\'';
                i += 2;
                break;
            case '0': { // \0 or \nnn octal
                size_t j = i + 1;
                std::string oct;
                while (j < len && oct.length() < 3 && input[j] >= '0' && input[j] <= '7') {
                    oct += input[j];
                    ++j;
                }
                if (!oct.empty()) {
                    char ch = static_cast<char>(std::stoi(oct, nullptr, 8));
                    oss << ch;
                    i = j;
                } else {
                    oss << '\0'; // A standalone \0
                    i += 2;
                }
            } break;

            case 'x': { // \xHH hexadecimal
                size_t j = i + 2;
                std::string hex;
                while (j < len && hex.length() < 2 && std::isxdigit(input[j])) {
                    hex += input[j];
                    ++j;
                }
                if (!hex.empty()) {
                    char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
                    oss << ch;
                    i = j;
                } else {
                    oss << '\\' << next;
                    i += 2;
                }
            } break;

            case 'u': { // \u{H...}: one Unicode code point, written as 1 to 6 hex digits
                size_t j       = i + 2;
                uint32_t value = 0;
                size_t digits  = 0;
                if (j < len && input[j] == '{') {
                    ++j;
                    while (j < len && digits < 6 &&
                           std::isxdigit(static_cast<unsigned char>(input[j]))) {
                        const char d = input[j];
                        value        = value * 16 + (d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10);
                        ++digits;
                        ++j;
                    }
                }
                if (digits > 0 && j < len && input[j] == '}' && camel::utf8::isScalarValue(value)) {
                    std::string encoded;
                    camel::utf8::encodeOne(value, encoded);
                    oss << encoded;
                    i = j + 1;
                } else {
                    oss << '\\' << next;
                    i += 2;
                }
            } break;

            default:
                oss << '\\' << next;
                i += 2;
                break;
            }
        } else {
            oss << c;
            ++i;
        }
    }

    return oss.str();
}
