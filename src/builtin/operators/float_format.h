// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A float as an argument of `format` (and of `print` / `println` with a format string).
 *
 * A float has one text (design principles 7.7): the shortest that reads back as the same number,
 * camel::utils::floatText. A placeholder without a format specification, `{}`, writes
 * that text, so that `format`, `string` and `println` agree; a placeholder with one
 * (`{:.2f}`, `{:>8}`, `{:e}`) asks for something else and is formatted by fmt as before.
 *
 * A bf16 or an f16 is a float type of its own, so `the same number` is an element of that type
 * and its text is the shortest that reads back as that element (camel/core/rtdata/half.h,
 * `halfText`; rule 2026-10-23, "reduced-precision-text"): the value is carried as the f32 it
 * widens to, exactly, and the format says whose text to write, so the one rule reaches `format`
 * and string interpolation as it reaches `println`.
 *
 * The sign of a NaN is not part of the text, with or without a specification (2.5.2 of the
 * language reference): fmt writes `-nan` for a NaN whose sign bit is set, so the value is made
 * positive before it is handed over (camel::utils::withoutNaNSign). The plain path needs none:
 * floatText never writes the sign.
 */

#pragma once

#include "camel/core/rtdata/half.h"
#include "camel/utils/number_text.h"

#include "fmt/format.h"

#include <algorithm>

namespace camel::builtin {

/// Which type's text plain `{}` writes. f32 and f64 print by their own rule; a bf16 or an f16
/// prints by `halfText` of its own pattern, and the value carried is still the exact f32 it
/// widens to, which is all a format specification needs.
enum class FloatTextFormat { Float32, Float64, BFloat16, Float16 };

template <typename T> struct LanguageFloat {
    T value;
    FloatTextFormat format = FloatTextFormat::Float32;
};

/// The text of a plain `{}` for a value of one of the four floating types. The half patterns are
/// rebuilt from the widened value's top or low bits: a bf16 and an f16 both widen to f32 exactly,
/// and storing the widened value back gives the same pattern (camel/core/rtdata/half.h).
template <typename T> std::string languageFloatText(T value, FloatTextFormat format) {
    switch (format) {
    case FloatTextFormat::BFloat16:
        return camel::core::rtdata::halfText(
            camel::core::rtdata::BFloat16::fromBits(camel::core::rtdata::bfloat16FromFloat(value)));
    case FloatTextFormat::Float16:
        return camel::core::rtdata::halfText(
            camel::core::rtdata::Float16::fromBits(camel::core::rtdata::float16FromFloat(value)));
    case FloatTextFormat::Float64:
        return camel::utils::floatText(static_cast<double>(value));
    case FloatTextFormat::Float32:
        break;
    }
    return camel::utils::floatText(static_cast<float>(value));
}

} // namespace camel::builtin

template <typename T>
struct fmt::formatter<camel::builtin::LanguageFloat<T>> : fmt::formatter<T> {
    constexpr auto parse(fmt::format_parse_context &ctx) {
        auto it = ctx.begin();
        plain_  = it == ctx.end() || *it == '}';
        return fmt::formatter<T>::parse(ctx);
    }
    template <typename FormatContext>
    auto format(const camel::builtin::LanguageFloat<T> &f, FormatContext &ctx) const {
        if (plain_) {
            const std::string text = camel::builtin::languageFloatText(f.value, f.format);
            return std::copy(text.begin(), text.end(), ctx.out());
        }
        return fmt::formatter<T>::format(camel::utils::withoutNaNSign(f.value), ctx);
    }

  private:
    bool plain_ = true;
};
