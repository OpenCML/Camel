// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The argument list of a log line, carried to the one translation unit that owns the string
 * formatting.
 *
 * A logging macro used to format its line with `std::format` inside `camel/utils/log.h`, the header
 * every translation unit includes: the whole format engine -- the format-string scanner, the sinks,
 * a formatter for every argument type of every call site -- was instantiated once per unit that can
 * log, whether or not that unit ever emitted a line. The values are therefore erased here into a
 * fixed shape and substituted in one place, `src/utils/log_format.cpp`, which is the equivalent of
 * what `camel/core/error/diagnostics/arguments.h` already does for a diagnostic message.
 *
 * A log call site carries a format *specifier* (`{:>4}`, `{:p}`), which a diagnostic message does
 * not, so the substitution site hands each field's specifier to the standard engine for the erased
 * value's own type: every specifier `std::format` accepts keeps working. A specifier the erased
 * type cannot take is reported by `std::formatter::parse` (`std::format_error`) instead of being
 * dropped, and a malformed format string -- or one naming more fields than the call has arguments
 * -- is refused while the call is compiled, which is where `std::format` refused it before.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace camel::log {

/// One value of a log line, erased to a kind the substitution site knows how to render.
struct LogArgument {
    enum class Kind : std::uint8_t {
        Text,       ///< A string or string view, as written.
        Signed32,   ///< A signed integer of 32 bits or fewer, in decimal.
        Signed64,   ///< A signed 64-bit integer.
        Unsigned32, ///< An unsigned integer of 32 bits or fewer.
        Unsigned64, ///< An unsigned 64-bit integer.
        Real32,     ///< A float; kept apart because its shortest form is not the double's.
        Real,       ///< A double.
        LongReal,   ///< A long double.
        Character,  ///< A char as written, not as its code point.
        Truth,      ///< A bool, printed `true` or `false`.
        Pointer,    ///< An address, printed as `0x...`.
    };

    Kind kind = Kind::Text;
    std::string_view text{};
    std::int64_t signedValue    = 0;
    std::uint64_t unsignedValue = 0;
    double real                 = 0.0;
    float real32                = 0.0f;
    long double longReal        = 0.0L;
    const void *pointer         = nullptr;
};

namespace detail {

template <typename> inline constexpr bool alwaysFalse = false;

/// \p value as one erased argument. Every type `std::format("{}", value)` accepts has a branch;
/// anything else is refused where it is written, so a value is never printed as something else.
template <typename T> LogArgument eraseLogArgument(T &&value) {
    using V = std::remove_cvref_t<T>;
    LogArgument argument;
    if constexpr (std::is_same_v<V, bool>) {
        argument.kind        = LogArgument::Kind::Truth;
        argument.signedValue = value ? 1 : 0;
    } else if constexpr (std::is_same_v<V, char>) {
        argument.kind        = LogArgument::Kind::Character;
        argument.signedValue = static_cast<std::int64_t>(value);
    } else if constexpr (std::is_same_v<V, float>) {
        argument.kind   = LogArgument::Kind::Real32;
        argument.real32 = value;
    } else if constexpr (std::is_same_v<V, double>) {
        argument.kind = LogArgument::Kind::Real;
        argument.real = value;
    } else if constexpr (std::is_same_v<V, long double>) {
        argument.kind     = LogArgument::Kind::LongReal;
        argument.longReal = value;
    } else if constexpr (std::is_integral_v<V>) {
        // Anything narrower than 64 bits is carried in 32: the digits and every specifier the
        // standard gives an integer are the same, and a negative `{:x}` of a 64-bit value is not.
        if constexpr (std::is_signed_v<V>) {
            if constexpr (sizeof(V) <= 4) {
                argument.kind        = LogArgument::Kind::Signed32;
                argument.signedValue = static_cast<std::int32_t>(value);
            } else {
                argument.kind        = LogArgument::Kind::Signed64;
                argument.signedValue = static_cast<std::int64_t>(value);
            }
        } else {
            if constexpr (sizeof(V) <= 4) {
                argument.kind          = LogArgument::Kind::Unsigned32;
                argument.unsignedValue = static_cast<std::uint32_t>(value);
            } else {
                argument.kind          = LogArgument::Kind::Unsigned64;
                argument.unsignedValue = static_cast<std::uint64_t>(value);
            }
        }
    } else if constexpr (std::is_convertible_v<T, std::string_view>) {
        argument.kind = LogArgument::Kind::Text;
        argument.text = std::string_view(value);
    } else if constexpr (std::is_convertible_v<T, const void *>) {
        argument.kind    = LogArgument::Kind::Pointer;
        argument.pointer = static_cast<const void *>(value);
    } else {
        static_assert(
            alwaysFalse<V>,
            "a log call can print a string, a number, a bool, a char or an address; this type is "
            "none of them, and printing it as something else would be wrong");
    }
    return argument;
}

/// How a log format string fails to match the arguments of the call.
enum class LogFormatError : std::uint8_t {
    None,
    UnmatchedBrace,     ///< A `{` or `}` with no partner.
    MalformedField,     ///< A field that is neither `{}`, `{N}` nor `{N:specifier}`.
    SpecifierWithField, ///< A specifier holding a replacement field (`{:{}}`), which the
                        ///< substitution site does not read.
    NoArgument,         ///< A field with no argument behind it.
    MixedIndexing,      ///< Automatic and explicit field indices in one string.
};

/// Reads \p pattern the way the substitution site does and says how it is wrong, so that the
/// mistake is a compile error at the call site instead of a surprise at run time. The specifier
/// itself is not judged here: which specifiers a value takes depends on the value.
constexpr LogFormatError checkLogFormat(std::string_view pattern, std::size_t argumentCount) {
    bool explicitIndex  = false;
    bool automaticIndex = false;
    std::size_t next    = 0;
    std::size_t i       = 0;
    while (i < pattern.size()) {
        const char current = pattern[i];
        if (current == '}') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '}') {
                i += 2;
                continue;
            }
            return LogFormatError::UnmatchedBrace;
        }
        if (current != '{') {
            ++i;
            continue;
        }
        if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
            i += 2;
            continue;
        }
        std::size_t cursor = i + 1;
        std::size_t index  = 0;
        bool digits        = false;
        while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9') {
            index  = index * 10 + static_cast<std::size_t>(pattern[cursor] - '0');
            digits = true;
            ++cursor;
        }
        if (cursor >= pattern.size()) {
            return LogFormatError::UnmatchedBrace;
        }
        if (pattern[cursor] == ':') {
            ++cursor;
            while (cursor < pattern.size() && pattern[cursor] != '}') {
                if (pattern[cursor] == '{') {
                    return LogFormatError::SpecifierWithField;
                }
                ++cursor;
            }
            if (cursor >= pattern.size()) {
                return LogFormatError::UnmatchedBrace;
            }
        }
        if (pattern[cursor] != '}') {
            return LogFormatError::MalformedField;
        }
        if (digits) {
            explicitIndex = true;
        } else {
            automaticIndex = true;
            index          = next++;
        }
        if (explicitIndex && automaticIndex) {
            return LogFormatError::MixedIndexing;
        }
        if (index >= argumentCount) {
            return LogFormatError::NoArgument;
        }
        i = cursor + 1;
    }
    return LogFormatError::None;
}

} // namespace detail

/// The reason a log format string was refused, as the compiler prints it.
constexpr const char *logFormatErrorText(detail::LogFormatError error) {
    switch (error) {
    case detail::LogFormatError::None:
        return "the log format string is well formed";
    case detail::LogFormatError::UnmatchedBrace:
        return "a log format string has a '{' or '}' with no partner";
    case detail::LogFormatError::MalformedField:
        return "a log format field is neither '{}', '{N}' nor '{N:specifier}'";
    case detail::LogFormatError::SpecifierWithField:
        return "a log format specifier cannot hold a replacement field of its own";
    case detail::LogFormatError::MixedIndexing:
        return "a log format string cannot mix '{}' with '{N}'";
    case detail::LogFormatError::NoArgument:
        return "a log format field has no argument behind it";
    }
    return "the log format string is malformed";
}

/// The format string of a log call. It can only be built from a string literal (the constructor is
/// `consteval`), which is what lets the field list be checked against the number of arguments while
/// the call is compiled -- the guarantee `std::format_string` gave the call sites before.
template <typename... Args> class LogFormatString {
  public:
    template <std::size_t N> consteval LogFormatString(const char (&text)[N]) : text_(text, N - 1) {
        // A mistake here is a defect in the call site, so it must stop the build rather than be
        // found later. The check reads the member, not the parameter, and reports by throwing:
        // inside an immediate function a parameter is not a constant expression, so it cannot be
        // the subject of a `static_assert`, while a member already built can be read. The throw is
        // never reached by a running program -- this constructor is always constant-evaluated --
        // and the compiler prints the thrown text as the reason. A string is thrown rather than a
        // `std::format_error` because naming that type here would put `<format>` back into every
        // unit that can log.
        const detail::LogFormatError error = detail::checkLogFormat(text_, sizeof...(Args));
        if (error != detail::LogFormatError::None) {
            throw logFormatErrorText(error);
        }
    }

    std::string_view text() const { return text_; }

  private:
    std::string_view text_;
};

/// Substitutes the erased \p arguments into \p pattern. Defined in `src/utils/log_format.cpp`,
/// which is the only translation unit that instantiates the format engine for logging; a pattern
/// that does not match its arguments throws `std::format_error`.
std::string
formatLogMessage(std::string_view pattern, const LogArgument *arguments, std::size_t count);

/// The entry point of every logging macro: erases \p args into one shape and formats them in the
/// translation unit above. Nothing of `std::format` is instantiated here or at a call site.
template <typename... Args>
inline std::string
formatLog(LogFormatString<std::type_identity_t<Args>...> format, Args &&...args) {
    LogArgument values[sizeof...(Args) == 0 ? 1 : sizeof...(Args)];
    std::size_t count = 0;
    ((values[count] = detail::eraseLogArgument(std::forward<Args>(args)), ++count), ...);
    return formatLogMessage(format.text(), values, count);
}

} // namespace camel::log