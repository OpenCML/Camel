// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one translation unit that formats diagnostic messages.
 *
 * `camel/core/error/diagnostics/arguments.h` erases the values of a message field; this file
 * substitutes them into the catalog pattern. Keeping the substitution here means the format
 * engine is instantiated once for the library instead of once per translation unit that reports a
 * diagnostic (see the note in the argument header).
 */

#include "camel/core/error/diagnostics.h"

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace camel::core::error {
namespace {

std::string renderArgument(const DiagnosticArgument &argument) {
    switch (argument.kind) {
    case DiagnosticArgument::Kind::Text:
        return std::string(argument.text);
    case DiagnosticArgument::Kind::Signed:
        return std::format("{}", argument.integer);
    case DiagnosticArgument::Kind::Unsigned:
        return std::format("{}", argument.natural);
    case DiagnosticArgument::Kind::Real:
        return std::format("{}", argument.real);
    case DiagnosticArgument::Kind::Real32:
        return std::format("{}", argument.real32);
    case DiagnosticArgument::Kind::Character:
        return std::format("{}", static_cast<char>(argument.integer));
    case DiagnosticArgument::Kind::Truth:
        return argument.integer != 0 ? "true" : "false";
    case DiagnosticArgument::Kind::Pointer:
        return std::format("{}", argument.pointer);
    }
    return std::string();
}

std::size_t fieldIndex(std::string_view pattern, std::size_t &cursor, bool &explicitIndex) {
    std::size_t index = 0;
    bool digits       = false;
    while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9') {
        index = index * 10 + static_cast<std::size_t>(pattern[cursor] - '0');
        digits = true;
        ++cursor;
    }
    explicitIndex = digits;
    return index;
}

/// Substitutes the catalog pattern: `{}` takes the next argument, `{N}` a chosen one, `{{` and `}}`
/// are literal braces. A diagnostic message has no format specifier (diagnostics/registry.tsv
/// carries none); a pattern that has one is refused rather than silently printed, because the
/// message it would produce is not the message the catalog states.
std::string substitute(std::string_view pattern, const DiagnosticArguments &arguments) {
    const auto &values = arguments.values();
    std::string text;
    text.reserve(pattern.size() + 32);

    std::size_t next          = 0;
    bool usedExplicitIndex    = false;
    bool usedAutomaticIndex   = false;
    std::size_t i             = 0;
    while (i < pattern.size()) {
        const char current = pattern[i];
        if (current == '}') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '}') {
                text.push_back('}');
                i += 2;
                continue;
            }
            throw std::format_error("unmatched '}' in a diagnostic message");
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
        std::size_t cursor    = i + 1;
        bool explicitIndex    = false;
        std::size_t index     = fieldIndex(pattern, cursor, explicitIndex);
        if (cursor >= pattern.size()) {
            throw std::format_error("unmatched '{' in a diagnostic message");
        }
        if (pattern[cursor] == ':') {
            throw std::format_error("a diagnostic message field cannot carry a format specifier");
        }
        if (pattern[cursor] != '}') {
            throw std::format_error("malformed field in a diagnostic message");
        }
        if (explicitIndex) {
            usedExplicitIndex = true;
        } else {
            usedAutomaticIndex = true;
            index              = next++;
        }
        if (usedExplicitIndex && usedAutomaticIndex) {
            throw std::format_error(
                "a diagnostic message mixes automatic and explicit field indices");
        }
        if (index >= values.size()) {
            throw std::format_error("a diagnostic message field has no argument");
        }
        text += renderArgument(values[index]);
        i = cursor + 1;
    }
    return text;
}

} // namespace

std::string
formatDiagnosticMessage(std::string_view pattern, const DiagnosticArguments &arguments) {
    try {
        return substitute(pattern, arguments);
    } catch (const std::format_error &error) {
        // A message that does not match its arguments is a defect in the catalog or in the call
        // site, never a language error: report it as an internal error with the reason.
        throw DiagnosticBuilder::of(InternalDiag::UnknownInternalError)
            .commit("Error formatting diagnostic message: " + std::string(error.what()));
    }
}

} // namespace camel::core::error