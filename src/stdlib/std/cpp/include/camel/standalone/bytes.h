// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `Bytes` (stdlib/bytes.cml, camel/core/rtdata/bytes.h) is a run of bytes and nothing else: a
 * length and the bytes. A translated program carries it as the array of bytes, `Array<uint8_t>` --
 * the source array's own representation, rank one, its extent the length (see
 * `ProgramValues::type`) -- so the kernels here are the four of the module and the crossing
 * `array:to_bytes` writes, all of them over that one type.
 *
 * The bytes the interpreter writes are the values' own, little-endian, in the order the elements
 * lie: `to_bytes` of `[1.0, 2.0] as f32[]` is `[0, 0, 128, 63, 0, 0, 0, 64]`. `sizeof` of the C++
 * element is that width, which is why the element types are the ones whose C++ form is the value
 * itself (a number, a bool, a half, a complex) and not the ones with a representation of their own.
 */

#pragma once
#include "array.h"
#include "core.h"
#include "opt.h"
#include "str.h"

#include "camel/utils/utf8.h"

#include <cstdint>
#include <string>

namespace camel::standalone {

/// `bytes:encode`: the UTF-8 bytes of the text. A string is always valid UTF-8 (design principles
/// 11.1), so nothing can fail.
inline Array<uint8_t> bytesOfText(const Str &text) {
    const std::string_view view = text.view();
    Array<uint8_t> out;
    out.reserve(view.size());
    for (const char c : view) {
        out.push(static_cast<uint8_t>(static_cast<unsigned char>(c)));
    }
    out.setExtents(std::initializer_list<std::int64_t>{static_cast<std::int64_t>(out.size())});
    return out;
}

/// `bytes:from_array`: the bytes with the given values, each of them 0..255; the first element
/// outside that range is a fault of `InvalidArgument`, as the interpreter's kernel says.
inline Array<uint8_t> bytesOfValues(const Array<int64_t> &values, const Site &site) {
    Array<uint8_t> out;
    out.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const int64_t value = values[i];
        if (value < 0 || value > 255) {
            // The site carries the message with one value in it; the fault names the element.
            fault(site, {std::to_string(i)});
        }
        out.push(static_cast<uint8_t>(value));
    }
    out.setExtents(std::initializer_list<std::int64_t>{static_cast<std::int64_t>(out.size())});
    return out;
}

/// `bytes:to_array`: the values of the bytes, each in 0..255, as `int[]` (`int` is `i64`).
inline Array<int64_t> valuesOfBytes(const Array<uint8_t> &bytes) {
    Array<int64_t> out;
    out.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out.push(static_cast<int64_t>(bytes[i]));
    }
    out.setExtents(std::initializer_list<std::int64_t>{static_cast<std::int64_t>(out.size())});
    return out;
}

/// `array:to_bytes`: the elements' own bytes, little-endian, in row-major order -- the run the
/// interpreter writes for `[1.0, 2.0] as f32[]`, four bytes an `f32`, one a `bool`, two a half,
/// eight an `f64` or a `c64`. A value of several dimensions lies flat, as it does in the array.
template <typename T> Array<uint8_t> bytesOfElements(const Array<T> &xs) {
    Array<uint8_t> out;
    out.reserve(xs.size() * sizeof(T));
    for (std::size_t i = 0; i < xs.size(); ++i) {
        T value = xs[i];
        const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
        for (std::size_t b = 0; b < sizeof(T); ++b) {
            out.push(bytes[b]);
        }
    }
    out.setExtents(std::initializer_list<std::int64_t>{static_cast<std::int64_t>(out.size())});
    return out;
}

/// `bytes:decode`: the text the bytes spell, or an `EncodingError` value -- not a fault: the bytes
/// come from the outside world. The error is the interpreter's, kind and message and the position
/// it names, `<bytes::decode>` (modules/bytes/module.cpp).
inline Fallible<Str> textOfBytes(const Array<uint8_t> &bytes) {
    std::string content;
    content.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        content.push_back(static_cast<char>(bytes[i]));
    }
    const size_t bad = camel::utf8::invalidOffset(content);
    if (bad != camel::utf8::npos) {
        return Fallible<Str>::failure(
            makeError(
                Str("EncodingError"),
                Str("invalid UTF-8 at byte " + std::to_string(bad)),
                Str("<bytes::decode>")));
    }
    return Fallible<Str>(Str(content));
}

} // namespace camel::standalone