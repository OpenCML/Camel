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
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

/// The one escape table of every string literal: an ordinary string, a triple-quoted string and
/// the pattern text of a formatted string are all decoded here. A formatted string has one more
/// escape of its own, `\${`, which the interpolation lexer and the AST builder decode before this
/// point; it is what keeps `${` from starting an interpolation, not a character escape. `\$` is a
/// character escape in every string, ordinary and triple-quoted included: a reader who writes it
/// gets a `$` whichever quotes they are in (H89, ruling 2).
///
/// `doubleBraces` is set for the pattern text of a formatted string. There a brace that an escape
/// produces (`\x7B`, `\u{7B}`) is a literal brace of the pattern, and `format` reads an undoubled
/// `{` as the start of a placeholder; the braces written in the text were already doubled by the
/// AST builder, so the ones decoded here are doubled now. Every other string passes false.
///
/// The escapes are `\0`, `\xHH`, `\u{H…}` and the named ones (H89: the set is the one of Rust).
/// Everything else is an unknown escape: it is kept as written and, when `unknownEscapes` is not
/// null, its spelling (the backslash and the character, or the whole octal-looking run) is
/// appended there, without repeats, for the caller to report as an unknown escape. In particular
/// an octal escape is cancelled: `\101` is the four characters `\101`, and `\01` is not the byte
/// 0x01 -- a `0` followed by another octal digit is an unknown escape too, not `\0` and a `1`.
inline std::string decodeEscapes(
    const std::string &input, bool doubleBraces = false,
    std::vector<std::string> *unknownEscapes = nullptr) {
    std::ostringstream oss;
    size_t i   = 0;
    size_t len = input.length();

    const auto noteUnknown = [unknownEscapes](std::string spelling) {
        if (!unknownEscapes) {
            return;
        }
        if (std::find(unknownEscapes->begin(), unknownEscapes->end(), spelling) ==
            unknownEscapes->end()) {
            unknownEscapes->push_back(std::move(spelling));
        }
    };
    // `\` at `at` followed by up to three octal digits: the run an unknown octal escape is named
    // by. Only ever called when the character after the backslash is 0 to 7.
    const auto octalRun = [&input, len](size_t at) {
        std::string run(1, '\\');
        for (size_t j = at + 1; j < len && run.size() <= 3 && input[j] >= '0' && input[j] <= '7';
             ++j) {
            run += input[j];
        }
        return run;
    };

    const auto put = [&oss, doubleBraces](char ch) {
        oss << ch;
        if (doubleBraces && (ch == '{' || ch == '}')) {
            oss << ch;
        }
    };

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
            case '`':
                // A backtick is a character like any other: in an ordinary string it is the
                // character itself, and in a formatted string the lexer has already used it to
                // find the end of the string. Every string decodes it here (H20 item 1).
                oss << '`';
                i += 2;
                break;
            case '$':
                // A `$` is a character like any other. The one place it is not is the `\${` of a
                // formatted string, which is a structural escape decoded before this point; every
                // other `\$`, in every string kind, is a plain dollar sign (H89, ruling 2).
                oss << '$';
                i += 2;
                break;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                // An octal escape without the leading zero: unknown since H89. It is kept as
                // written, and reported with the whole run it spells.
                noteUnknown(octalRun(i));
                oss << '\\' << next;
                i += 2;
                break;
            case '0': { // a bare \0 is the null byte; \0 followed by an octal digit is unknown
                if (i + 2 < len && input[i + 2] >= '0' && input[i + 2] <= '7') {
                    noteUnknown(octalRun(i));
                    oss << '\\' << next;
                    i += 2;
                    break;
                }
                oss << '\0';
                i += 2;
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
                    put(ch);
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
                    for (char ch : encoded) {
                        put(ch);
                    }
                    i = j + 1;
                } else {
                    oss << '\\' << next;
                    i += 2;
                }
            } break;

            default:
                // Any other backslash pair is an unknown escape: kept as written, reported.
                noteUnknown(std::string(1, '\\') + next);
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
