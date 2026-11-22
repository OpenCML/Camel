// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The argument list of a diagnostic message, carried to the one translation unit that owns the
 * message formatting.
 *
 * A diagnostic message is a catalog entry with `{}` / `{N}` fields (diagnostics/registry.tsv).
 * Formatting it with `std::format` here, in the header every translation unit includes, would
 * instantiate the whole format engine -- format-string scanning, the sinks, the per-type
 * formatters -- once per translation unit: measured at 1.5-2.5 CPU-seconds on the largest units,
 * and paid by all 294 units of libcamel_objects for the handful of them that report diagnostics.
 * The values are therefore erased here into a fixed shape and formatted once, in
 * `src/core/error/diagnostics/format.cpp`.
 *
 * Erasing a value is a copy of its text or its numeric representation; the argument is only read,
 * and a borrowed `std::string_view` stays valid because the caller's temporary lives to the end of
 * the `commit` call that consumes it.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace camel::core::error {

/// One value substituted into a diagnostic message field.
struct DiagnosticArgument {
    enum class Kind : std::uint8_t {
        Text,      ///< A string as written.
        Signed,    ///< A signed integer, printed in decimal.
        Unsigned,  ///< An unsigned integer, printed in decimal.
        Real,      ///< A double, printed in its shortest round-trip form.
        Real32,    ///< A float; kept apart because its shortest form differs from the double's.
        Character, ///< A character as written, not as its code point.
        Truth,     ///< A bool, printed `true` or `false`.
        Pointer,   ///< An address, printed as `0x...`.
    };

    Kind kind = Kind::Text;
    std::string_view text{};
    std::int64_t integer      = 0;
    std::uint64_t natural     = 0;
    double real               = 0.0;
    float real32              = 0.0f;
    const void *pointer       = nullptr;
};

/// The ordered arguments of one diagnostic message.
class DiagnosticArguments {
  public:
    void add(std::string_view value) { pushText(value); }
    void add(const std::string &value) { pushText(std::string_view(value)); }
    void add(const char *value) { pushText(std::string_view(value != nullptr ? value : "")); }

    void add(char value) {
        DiagnosticArgument argument;
        argument.kind    = DiagnosticArgument::Kind::Character;
        argument.integer = value;
        values_.push_back(argument);
    }

    void add(bool value) {
        DiagnosticArgument argument;
        argument.kind    = DiagnosticArgument::Kind::Truth;
        argument.integer = value ? 1 : 0;
        values_.push_back(argument);
    }

    void add(float value) {
        DiagnosticArgument argument;
        argument.kind   = DiagnosticArgument::Kind::Real32;
        argument.real32 = value;
        values_.push_back(argument);
    }

    void add(double value) {
        DiagnosticArgument argument;
        argument.kind = DiagnosticArgument::Kind::Real;
        argument.real = value;
        values_.push_back(argument);
    }

    void add(const void *value) {
        DiagnosticArgument argument;
        argument.kind    = DiagnosticArgument::Kind::Pointer;
        argument.pointer = value;
        values_.push_back(argument);
    }

    void add(std::nullptr_t) { add(static_cast<const void *>(nullptr)); }

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
    void add(T value) {
        DiagnosticArgument argument;
        if constexpr (std::is_signed_v<T>) {
            argument.kind    = DiagnosticArgument::Kind::Signed;
            argument.integer = static_cast<std::int64_t>(value);
        } else {
            argument.kind    = DiagnosticArgument::Kind::Unsigned;
            argument.natural = static_cast<std::uint64_t>(value);
        }
        values_.push_back(argument);
    }

    const std::vector<DiagnosticArgument> &values() const { return values_; }

  private:
    void pushText(std::string_view value) {
        DiagnosticArgument argument;
        argument.kind = DiagnosticArgument::Kind::Text;
        argument.text = value;
        values_.push_back(argument);
    }

    std::vector<DiagnosticArgument> values_;
};

/// Substitutes \p arguments into the `{}` / `{N}` fields of \p pattern. Defined in
/// `src/core/error/diagnostics/format.cpp`, which is the only translation unit that instantiates
/// the format engine for diagnostics. A malformed pattern or a missing argument throws
/// `std::format_error`, and a formatting failure is reported as an internal diagnostic -- the
/// behaviour the previous in-header `std::vformat` call had.
std::string
formatDiagnosticMessage(std::string_view pattern, const DiagnosticArguments &arguments);

} // namespace camel::core::error