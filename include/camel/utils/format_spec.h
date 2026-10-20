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
 * Splitting a format specification into the part that formats a number and the part that lays
 * the text out: `[[fill]align][sign][#][0][width][.precision][type]`.
 *
 * A complex number is two numbers under one specification (complex_format.h, and the language
 * reference): the precision, the type and the `#` flag say how each part is written, while the
 * fill, the alignment and the width lay out the text of the whole number. This file is the one
 * place that knows the grammar, shared by the interpreter's formatter and the standalone
 * runtime of `std::cpp`, so the two agree on what a specification means for a complex number.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace camel::utils {

/// The length in bytes of the first code point of `text` (a fill may be any character).
inline std::size_t firstCodePointLength(std::string_view text) {
    std::size_t length = 1;
    while (length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
        ++length;
    }
    return length;
}

inline bool isFormatAlignment(char c) { return c == '<' || c == '>' || c == '^' || c == '='; }

/// One format specification, split at the width: `number` is what formats one part.
struct NumberFormatSpec {
    std::string fill = " ";
    char align       = '\0'; // '<', '>', '^', '=' or none
    std::string number;      // [sign][#][0][.precision][L][type]
    int width        = 0;    // 0: no width
    bool zeroPadded  = false; // the `0` flag, or a fill of `0` (`0>8`): not for a complex number
    bool equalAlign  = false; // the `=` alignment: not for a complex number
};

/// `spec` split into the number part and the layout part. A malformed specification is left
/// for the number formatter of the caller to reject (this function reads a specification that
/// the caller's formatter accepted or is about to check).
inline NumberFormatSpec splitNumberFormatSpec(std::string_view spec) {
    NumberFormatSpec out;
    std::size_t i = 0;
    if (!spec.empty() && isFormatAlignment(spec[0])) {
        out.align = spec[0];
        i         = 1;
    } else if (spec.size() > 1) {
        const std::size_t fill = firstCodePointLength(spec);
        if (fill < spec.size() && isFormatAlignment(spec[fill])) {
            out.fill  = std::string(spec.substr(0, fill));
            out.align = spec[fill];
            i         = fill + 1;
        }
    }
    if (out.align == '=') {
        out.equalAlign = true;
    }
    if (out.fill == "0") {
        out.zeroPadded = true;
    }
    if (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' ')) {
        out.number += spec[i++];
    }
    if (i < spec.size() && spec[i] == '#') {
        out.number += spec[i++];
    }
    if (i < spec.size() && spec[i] == '0') {
        out.zeroPadded = true;
        out.number += spec[i++];
    }
    int width = 0;
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
        width = width * 10 + (spec[i] - '0');
        ++i;
    }
    out.width = width;
    out.number += spec.substr(i);
    return out;
}

/// `text` in a field of `width` code points: the fill on the side the alignment asks for
/// (`<` left-aligned, `^` centered, everything else right-aligned, which is what a number
/// does).
inline std::string padFormatText(std::string_view text, std::string_view fill, char align, int width) {
    std::size_t length = 0;
    for (unsigned char c : text) {
        length += (c & 0xC0) != 0x80;
    }
    if (width <= 0 || length >= static_cast<std::size_t>(width)) {
        return std::string(text);
    }
    const std::size_t padding = static_cast<std::size_t>(width) - length;
    std::string left;
    std::string right;
    std::size_t leftCount = 0;
    if (align == '<') {
        leftCount = 0;
    } else if (align == '^') {
        leftCount = padding / 2;
    } else {
        leftCount = padding;
    }
    for (std::size_t k = 0; k < leftCount; ++k) {
        left += fill;
    }
    for (std::size_t k = 0; k < padding - leftCount; ++k) {
        right += fill;
    }
    return left + std::string(text) + right;
}

} // namespace camel::utils