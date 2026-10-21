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
 * A complex number as an argument of `format` (and of `print` / `println` with a format
 * string).
 *
 * A placeholder without a format specification, `{}`, writes the language's complex text,
 * camel::utils::complexText (`3+4i`). A specification applies to the value as what it is — two
 * numbers under one specification: the precision, the type and the `#` flag write each part,
 * so `{:.2f}` is `3.00+4.00i` and `{:e}` is `3.000000e+00+4.000000e+00i`; the fill, the
 * alignment and the width lay out the text of the whole number, so `{:>12.2f}` is
 * `  3.00+4.00i`. The sign flag belongs to the number as a whole (`{:+}` is `+3+4i`); the sign
 * of the imaginary part is the separator between the parts. Zero padding and the `=`
 * alignment do not mean anything for two numbers written together and are format errors, as
 * they are in Python.
 *
 * The parts are formatted by fmt as the floats of the value's width, so a specification that
 * is an error for a float (`{:s}`, `{:d}`) is an error for a complex number too. A NaN part is
 * written `nan`, never `-nan`, in both forms: the sign of a NaN is not part of the text (2.5.2
 * of the language reference), and the value handed to fmt is made positive first.
 */

#pragma once

#include "camel/utils/format_spec.h"
#include "camel/utils/number_text.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace camel::builtin {

template <typename T> struct LanguageComplex {
    T real;
    T imaginary;
};

namespace format_detail {

template <typename T>
std::string specText(const LanguageComplex<T> &value, const camel::utils::NumberFormatSpec &spec) {
    std::string signedSpec = spec.number;
    std::string magnitudeSpec;
    if (!signedSpec.empty() &&
        (signedSpec[0] == '+' || signedSpec[0] == '-' || signedSpec[0] == ' ')) {
        magnitudeSpec = signedSpec.substr(1);
    } else {
        magnitudeSpec = signedSpec;
    }
    T real              = camel::utils::withoutNaNSign(value.real);
    std::string text    = fmt::vformat("{:" + signedSpec + "}", fmt::make_format_args(real));
    const bool negative = !std::isnan(value.imaginary) && std::signbit(value.imaginary);
    T magnitude = camel::utils::withoutNaNSign(negative ? -value.imaginary : value.imaginary);
    text += negative ? '-' : '+';
    text += fmt::vformat("{:" + magnitudeSpec + "}", fmt::make_format_args(magnitude));
    text += 'i';
    return camel::utils::padFormatText(text, spec.fill, spec.align, spec.width);
}

} // namespace format_detail

} // namespace camel::builtin

template <typename T>
struct fmt::formatter<camel::builtin::LanguageComplex<T>> : fmt::formatter<T> {
    constexpr auto parse(fmt::format_parse_context &ctx) {
        const char *it  = ctx.begin();
        const char *end = ctx.end();
        std::string text;
        while (it != end && *it != '}') {
            text.push_back(*it);
            ++it;
        }
        plain_ = text.empty();
        if (!plain_) {
            spec_ = camel::utils::splitNumberFormatSpec(text);
            if (spec_.zeroPadded) {
                throw fmt::format_error(
                    "Zero padding is not allowed in complex format specifier");
            }
            if (spec_.equalAlign) {
                throw fmt::format_error(
                    "'=' alignment is not allowed in complex format specifier");
            }
        }
        return fmt::formatter<T>::parse(ctx);
    }

    template <typename FormatContext>
    auto format(const camel::builtin::LanguageComplex<T> &value, FormatContext &ctx) const {
        const std::string text = plain_ ? camel::utils::complexText<T>(value.real, value.imaginary)
                                        : camel::builtin::format_detail::specText(value, spec_);
        return std::copy(text.begin(), text.end(), ctx.out());
    }

  private:
    camel::utils::NumberFormatSpec spec_;
    bool plain_ = true;
};