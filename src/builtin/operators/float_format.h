// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A float as an argument of `format` (and of `print` / `println` with a format string).
 *
 * A float has one text (design principles 7.7): the shortest that reads back as the same
 * number, camel::utils::floatText. A placeholder without a format specification, `{}`, writes
 * that text, so that `format`, `string` and `println` agree; a placeholder with one
 * (`{:.2f}`, `{:>8}`, `{:e}`) asks for something else and is formatted by fmt as before.
 *
 * The sign of a NaN is not part of the text, with or without a specification (2.5.2 of the
 * language reference): fmt writes `-nan` for a NaN whose sign bit is set, so the value is made
 * positive before it is handed over (camel::utils::withoutNaNSign). The plain path needs none:
 * floatText never writes the sign.
 */

#pragma once

#include "camel/utils/number_text.h"

#include "fmt/format.h"

#include <algorithm>

namespace camel::builtin {

template <typename T> struct LanguageFloat {
    T value;
};

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
            const std::string text = camel::utils::floatText(f.value);
            return std::copy(text.begin(), text.end(), ctx.out());
        }
        return fmt::formatter<T>::format(camel::utils::withoutNaNSign(f.value), ctx);
    }

  private:
    bool plain_ = true;
};
