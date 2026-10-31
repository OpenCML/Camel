// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two string kernels whose result is a value of another type: the code points as `int[]` and
 * the position of a needle as `int?`. They live here and not in `str.h` because the headers they
 * return -- `Array` and `Opt` -- are declared after it (`array.h` and `opt.h` include `str.h`, not
 * the other way round), and both are values the target already carries: no type is added for them.
 *
 * Each is the interpreter's own kernel, statement for statement (modules/str/operators.cpp):
 * `__str_codepoints__` walks the text a code point at a time (`camel::utf8::decodeOne`, which
 * advances the position), and `__str_find__` finds the needle's byte (`std::string_view::find`) and
 * reports the character position (`camel::utf8::indexOf`), or the null.
 */

#pragma once
#include "array.h"
#include "opt.h"
#include "str.h"

#include "camel/utils/utf8.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace camel::standalone {

/// `str:codepoints`: the code points of the text as numbers, one step a code point. A string is
/// always valid UTF-8 (design principles 11.1), so the walk always moves forward.
inline Array<int64_t> codePoints(const Str &s) {
    const std::string_view text = s.view();
    Array<int64_t> out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        out.push(static_cast<int64_t>(camel::utf8::decodeOne(text, pos)));
    }
    out.setExtents(std::initializer_list<std::int64_t>{static_cast<std::int64_t>(out.size())});
    return out;
}

/// `str:from_codepoints`: the text the code points spell, or an `EncodingError` value naming the
/// first number that is not a Unicode code point -- a value-level error and not a fault, as the
/// interpreter's kernel says (`modules/str/operators.cpp`). A result longer than a string can be
/// is the one fault: `InvalidArgument` at the site, whose two words the caller writes.
inline Fallible<Str> textOfCodePoints(const Array<int64_t> &values, const Site &site) {
    std::string out;
    out.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const int64_t point = values[i];
        if (!camel::utf8::isScalarValue(point)) {
            return Fallible<Str>::failure(
                makeError(
                    Str("EncodingError"),
                    Str(
                        "element " + std::to_string(i) + " is " + std::to_string(point) +
                        ", not a Unicode code point"),
                    Str("<str::from_codepoints>")));
        }
        camel::utf8::encodeOne(static_cast<uint32_t>(point), out);
    }
    if (out.size() > std::numeric_limits<uint32_t>::max()) [[unlikely]] {
        fault(site);
    }
    return Fallible<Str>(Str(out));
}

/// `str:find`: the position of the first occurrence of `sub`, counted in characters and not in
/// bytes, or the null when the text does not hold it. A needle that is absent is the null and not
/// a fault; a needle in a valid text starts on a character boundary, which is why the count is
/// `indexOf`'s.
inline Opt<int64_t> findText(const Str &s, const Str &sub) {
    const std::size_t at = s.view().find(sub.view());
    if (at == std::string_view::npos) {
        return Opt<int64_t>();
    }
    return Opt<int64_t>(static_cast<int64_t>(camel::utf8::indexOf(s.view(), at)));
}

} // namespace camel::standalone