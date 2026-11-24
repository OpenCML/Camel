// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * UTF-8, in one place (design principles 11.1: a `string` is always valid UTF-8, and its
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

inline void encodeOne(uint32_t cp, std::string &out);

/**
 * The wide form of a text: what a `wchar_t` string holds, and what the Windows interfaces take
 * (`GetEnvironmentVariableW`, a `wmain` argument, the wide form of a `std::filesystem::path`).
 * On Windows a `wchar_t` is 16 bits and a code point above U+FFFF becomes a surrogate pair; on
 * the other platforms it is 32 bits and holds the code point itself.
 *
 * A byte that is not valid UTF-8 becomes U+FFFD, the way a Windows conversion treats it, so
 * this never fails. A Camel `string` is valid by construction (design principles 11.1); the
 * replacement only concerns text the system handed back.
 */
inline std::wstring toWide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const auto first = static_cast<unsigned char>(s[i]);
        if (first < 0x80) {
            out.push_back(static_cast<wchar_t>(first));
            ++i;
            continue;
        }
        const size_t length = sequenceLength(first);
        uint32_t value      = first & (0xFFu >> (length + 1));
        size_t continuation = 1;
        bool valid          = i + length <= s.size();
        for (; valid && continuation < length; ++continuation) {
            const auto next = static_cast<unsigned char>(s[i + continuation]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
            } else {
                value = (value << 6) | (next & 0x3F);
            }
        }
        // The same rejection rules as invalidOffset: an overlong form, a surrogate, a value above
        // U+10FFFF. One replacement character, then decoding resumes at the next byte.
        const uint32_t least = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
        if (!valid || value < least || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }
        i += length;
        if constexpr (sizeof(wchar_t) == 2) {
            if (value >= 0x10000) {
                value -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
            } else {
                out.push_back(static_cast<wchar_t>(value));
            }
        } else {
            out.push_back(static_cast<wchar_t>(value));
        }
    }
    return out;
}

/// The UTF-8 form of a wide text. A lone surrogate becomes U+FFFD.
inline std::string fromWide(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t value = static_cast<uint32_t>(w[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (value >= 0xD800 && value <= 0xDBFF) {
                const uint32_t low = i + 1 < w.size() ? static_cast<uint32_t>(w[i + 1]) : 0;
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                } else {
                    value = 0xFFFD;
                }
            } else if (value >= 0xDC00 && value <= 0xDFFF) {
                value = 0xFFFD;
            }
        }
        encodeOne(value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ? 0xFFFD : value, out);
    }
    return out;
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
