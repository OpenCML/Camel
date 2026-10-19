/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * UTF-8, in one place (design principles 2.17: a `string` is always valid UTF-8, and its
 * indices and length count code points).
 *
 * Header-only and standard-only, so that the runtime of the generated C++ programs
 * (include/camel/standalone) and the interpreter share the same code.
 *
 * Decoding is strict: an overlong form, a surrogate (U+D800..U+DFFF), a value above U+10FFFF,
 * a stray continuation byte and a sequence cut short are all invalid. Every function that
 * counts or indexes assumes its input is valid; `invalidOffset` is how a text is checked.
 * ASCII-only text (the common case) is handled eight bytes at a time.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace camel::utf8 {

inline constexpr size_t npos = static_cast<size_t>(-1);

/// Whether every byte of the text is below 0x80.
inline bool isAscii(std::string_view s) noexcept {
    const char *p           = s.data();
    const char *end         = p + s.size();
    constexpr uint64_t high = 0x8080808080808080ull;
    while (end - p >= 8) {
        uint64_t word;
        std::memcpy(&word, p, 8);
        if ((word & high) != 0)
            return false;
        p += 8;
    }
    for (; p < end; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80)
            return false;
    }
    return true;
}

/// The offset of the first byte that is not part of valid UTF-8, or `npos` when the whole text
/// is valid.
inline size_t invalidOffset(std::string_view s) noexcept {
    size_t i       = 0;
    const size_t n = s.size();
    while (i < n) {
        const auto first = static_cast<unsigned char>(s[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        const size_t start = i++;
        size_t count;
        uint32_t value, least;
        if (first >= 0xC2 && first <= 0xDF)
            count = 1, value = first & 0x1F, least = 0x80;
        else if (first >= 0xE0 && first <= 0xEF)
            count = 2, value = first & 0x0F, least = 0x800;
        else if (first >= 0xF0 && first <= 0xF4)
            count = 3, value = first & 0x07, least = 0x10000;
        else
            return start;
        if (count > n - i)
            return start;
        for (size_t k = 0; k < count; ++k) {
            const auto next = static_cast<unsigned char>(s[i++]);
            if ((next & 0xC0) != 0x80)
                return start;
            value = (value << 6) | (next & 0x3F);
        }
        if (value < least || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            return start;
    }
    return npos;
}

inline bool isValid(std::string_view s) noexcept { return invalidOffset(s) == npos; }

/// The number of code points of a valid text.
inline size_t count(std::string_view s) noexcept {
    size_t n = 0;
    for (unsigned char c : s)
        n += (c & 0xC0) != 0x80;
    return n;
}

/// The length in bytes of the code point whose first byte is `first`.
inline size_t sequenceLength(unsigned char first) noexcept {
    if (first < 0x80)
        return 1;
    if (first >= 0xF0)
        return 4;
    if (first >= 0xE0)
        return 3;
    return 2;
}

/// The byte offset at which code point number `index` of a valid text starts; `s.size()` when
/// `index` equals the number of code points, `npos` when it is larger.
inline size_t offsetOf(std::string_view s, size_t index) noexcept {
    size_t at = 0;
    while (index > 0 && at < s.size()) {
        at += sequenceLength(static_cast<unsigned char>(s[at]));
        --index;
    }
    return index == 0 && at <= s.size() ? at : npos;
}

/// The number of code points before byte offset `byteOffset` (which must be on a boundary).
inline size_t indexOf(std::string_view s, size_t byteOffset) noexcept {
    return count(s.substr(0, byteOffset));
}

/// Decodes the code point that starts at `pos` of a valid text and advances `pos` past it.
inline uint32_t decodeOne(std::string_view s, size_t &pos) noexcept {
    const auto b0 = static_cast<unsigned char>(s[pos]);
    if (b0 < 0x80) {
        ++pos;
        return b0;
    }
    const size_t length = sequenceLength(b0);
    uint32_t value      = b0 & (0xFFu >> (length + 1));
    for (size_t k = 1; k < length; ++k)
        value = (value << 6) | (static_cast<unsigned char>(s[pos + k]) & 0x3F);
    pos += length;
    return value;
}

inline bool isScalarValue(int64_t cp) noexcept {
    return cp >= 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

inline void encodeOne(uint32_t cp, std::string &out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace camel::utf8
