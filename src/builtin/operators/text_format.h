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
 * Text as an argument of `format` (and of `print` / `println` with a format string).
 *
 * A string is valid UTF-8 and its length is its number of code points (design principles
 * 2.17). fmt pads by the display width of a character (a wide character counts 2), which is
 * neither; this type reads the specification `[[fill]align][width][.precision][s]` itself and
 * counts code points, as the runtime of the generated C++ programs does.
 */

#pragma once

#include "camel/utils/utf8.h"

#include "fmt/format.h"

#include <algorithm>
#include <string>

namespace camel::builtin {

struct CodePointText {
    std::string text;
};

} // namespace camel::builtin

template <> struct fmt::formatter<camel::builtin::CodePointText> {
    std::string fill_ = " ";
    char align_       = '<';
    size_t width_     = 0;
    long precision_   = -1;

    auto parse(fmt::format_parse_context &ctx) {
        auto it  = ctx.begin();
        auto end = ctx.end();
        const std::string_view spec(it, static_cast<size_t>(end - it));
        size_t at       = 0;
        const auto fail = [] { throw fmt::format_error("invalid format specifier"); };
        const auto isAlign = [](char c) { return c == '<' || c == '>' || c == '^'; };
        // [[fill]align]: a fill is one code point followed by an alignment.
        if (!spec.empty() && spec[0] != '}') {
            const size_t first = camel::utf8::sequenceLength(static_cast<unsigned char>(spec[0]));
            if (first < spec.size() && isAlign(spec[first])) {
                fill_  = std::string(spec.substr(0, first));
                align_ = spec[first];
                at     = first + 1;
            } else if (isAlign(spec[0])) {
                align_ = spec[0];
                at     = 1;
            }
        }
        while (at < spec.size() && spec[at] >= '0' && spec[at] <= '9') {
            width_ = width_ * 10 + static_cast<size_t>(spec[at] - '0');
            ++at;
        }
        if (at < spec.size() && spec[at] == '.') {
            ++at;
            if (at >= spec.size() || spec[at] < '0' || spec[at] > '9')
                throw fmt::format_error("missing precision specifier");
            precision_ = 0;
            while (at < spec.size() && spec[at] >= '0' && spec[at] <= '9') {
                precision_ = precision_ * 10 + (spec[at] - '0');
                ++at;
            }
        }
        if (at < spec.size() && spec[at] == 's')
            ++at;
        // `{}` reaches a custom type with an empty range.
        if (at < spec.size() && spec[at] != '}')
            fail();
        return it + at;
    }

    template <typename FormatContext>
    auto format(const camel::builtin::CodePointText &t, FormatContext &ctx) const {
        std::string_view body = t.text;
        if (precision_ >= 0) {
            const size_t keep = camel::utf8::offsetOf(body, static_cast<size_t>(precision_));
            if (keep != camel::utf8::npos)
                body = body.substr(0, keep);
        }
        const size_t have    = camel::utf8::count(body);
        const size_t missing = width_ > have ? width_ - have : 0;
        const size_t before  = align_ == '>' ? missing : align_ == '^' ? missing / 2 : 0;
        const size_t after   = missing - before;
        auto out             = ctx.out();
        for (size_t i = 0; i < before; ++i)
            out = std::copy(fill_.begin(), fill_.end(), out);
        out = std::copy(body.begin(), body.end(), out);
        for (size_t i = 0; i < after; ++i)
            out = std::copy(fill_.begin(), fill_.end(), out);
        return out;
    }
};
