// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one translation unit that formats a log line.
 *
 * `camel/utils/log_format.h` erases the values of a log call; this file substitutes them into its
 * format string, so the format engine is instantiated once for the library instead of once per
 * translation unit that can log (see the note in the argument header). It answers the two things a
 * diagnostic message does not need:
 *
 *   * a *specifier*. A log call site writes `{:>4}` or `{:p}`, so each field's specifier is handed
 *     to `std::formatter` for the erased value's own type. Every specifier `std::format` accepts
 *     keeps working, and one the type cannot take throws `std::format_error` from the standard
 *     parser rather than being dropped;
 *   * a *malformed* string. A pattern that does not match its arguments is a defect at the call
 *     site, and `camel/utils/log_format.h` refuses one while it is compiled; reaching here with one
 *     is reported as `std::format_error` naming the pattern.
 */

#include "camel/utils/log_format.h"

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace camel::log {
namespace {

/// One erased value through the standard engine, with the field's specifier when it has one. This
/// is the only place a formatter is instantiated for a log argument.
template <typename T> std::string render(const T &value, std::string_view specifier) {
    if (specifier.empty()) {
        return std::format("{}", value);
    }
    std::string pattern = "{:";
    pattern.append(specifier);
    pattern.push_back('}');
    return std::vformat(pattern, std::make_format_args(value));
}

std::string renderArgument(const LogArgument &argument, std::string_view specifier) {
    switch (argument.kind) {
    case LogArgument::Kind::Text:
        return render(argument.text, specifier);
    case LogArgument::Kind::Signed32:
        return render(static_cast<std::int32_t>(argument.signedValue), specifier);
    case LogArgument::Kind::Signed64:
        return render(argument.signedValue, specifier);
    case LogArgument::Kind::Unsigned32:
        return render(static_cast<std::uint32_t>(argument.unsignedValue), specifier);
    case LogArgument::Kind::Unsigned64:
        return render(argument.unsignedValue, specifier);
    case LogArgument::Kind::Real32:
        return render(argument.real32, specifier);
    case LogArgument::Kind::Real:
        return render(argument.real, specifier);
    case LogArgument::Kind::LongReal:
        return render(argument.longReal, specifier);
    case LogArgument::Kind::Character:
        return render(static_cast<char>(argument.signedValue), specifier);
    case LogArgument::Kind::Truth: {
        const bool value = argument.signedValue != 0;
        return render(value, specifier);
    }
    case LogArgument::Kind::Pointer:
        return render(argument.pointer, specifier);
    }
    return std::string();
}

/// Substitutes the erased values into \p pattern: `{}` takes the next argument, `{N}` a chosen one,
/// `{{` and `}}` are literal braces, and `:` opens a specifier, which `renderArgument` gives to the
/// standard formatter of that value's type. The reading is the one `checkLogFormat` accepted, so a
/// pattern that gets here is structurally sound and a failure is a defect the call site did not
/// catch at compile time.
std::string substitute(std::string_view pattern, const LogArgument *arguments, std::size_t count) {
    std::string text;
    text.reserve(pattern.size() + 32);

    std::size_t next        = 0;
    bool usedExplicitIndex  = false;
    bool usedAutomaticIndex = false;
    std::size_t i           = 0;
    while (i < pattern.size()) {
        const char current = pattern[i];
        if (current == '}') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '}') {
                text.push_back('}');
                i += 2;
                continue;
            }
            throw std::format_error("unmatched '}' in a log format string");
        }
        if (current != '{') {
            text.push_back(current);
            ++i;
            continue;
        }
        if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
            text.push_back('{');
            i += 2;
            continue;
        }
        std::size_t cursor = i + 1;
        std::size_t index  = 0;
        bool explicitIndex = false;
        while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9') {
            index         = index * 10 + static_cast<std::size_t>(pattern[cursor] - '0');
            explicitIndex = true;
            ++cursor;
        }
        if (cursor >= pattern.size()) {
            throw std::format_error("unmatched '{' in a log format string");
        }
        std::string_view specifier;
        if (pattern[cursor] == ':') {
            const std::size_t specifierStart = cursor + 1;
            const std::size_t specifierEnd   = pattern.find('}', specifierStart);
            if (specifierEnd == std::string_view::npos) {
                throw std::format_error("unmatched '{' in a log format string");
            }
            specifier = pattern.substr(specifierStart, specifierEnd - specifierStart);
            cursor    = specifierEnd;
        }
        if (pattern[cursor] != '}') {
            throw std::format_error("malformed field in a log format string");
        }
        if (explicitIndex) {
            usedExplicitIndex = true;
        } else {
            usedAutomaticIndex = true;
            index              = next++;
        }
        if (usedExplicitIndex && usedAutomaticIndex) {
            throw std::format_error(
                "a log format string mixes automatic and explicit field indices");
        }
        if (index >= count) {
            throw std::format_error("a log format field has no argument");
        }
        text += renderArgument(arguments[index], specifier);
        i = cursor + 1;
    }
    return text;
}

bool hasNoField(std::string_view pattern) {
    return pattern.find('{') == std::string_view::npos &&
           pattern.find('}') == std::string_view::npos;
}

} // namespace

std::string
formatLogMessage(std::string_view pattern, const LogArgument *arguments, std::size_t count) {
    if (hasNoField(pattern)) {
        return std::string(pattern);
    }
    try {
        return substitute(pattern, arguments, count);
    } catch (const std::format_error &error) {
        // A line that does not match its arguments is a defect in the call site, never a language
        // error, and it must not be swallowed: name the pattern and let the reason through.
        throw std::format_error(
            "log format string \"" + std::string(pattern) + "\": " + error.what());
    }
}

} // namespace camel::log