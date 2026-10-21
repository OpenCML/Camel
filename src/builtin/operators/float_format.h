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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
