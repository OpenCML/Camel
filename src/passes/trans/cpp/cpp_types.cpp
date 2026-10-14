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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "cpp_types.h"

#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <system_error>

namespace camel::passes::cpp {

using camel::core::type::TypeCode;

std::optional<std::string> cppTypeOf(const Type *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    switch (type->code()) {
    case TypeCode::Int32:
        return "int32_t";
    case TypeCode::Int64:
        return "int64_t";
    case TypeCode::Float32:
        return "float";
    case TypeCode::Float64:
        return "double";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::String:
        return "rt::Str";
    case TypeCode::Void:
        return "void";
    default:
        return std::nullopt;
    }
}

bool isVoid(const Type *type) { return type != nullptr && type->code() == TypeCode::Void; }

std::string cppStringLiteral(std::string_view bytes) {
    std::string out = "\"";
    for (unsigned char c : bytes) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c >= 0x20 && c < 0x7F) {
                // '?' twice could start a trigraph before C++17; spelled out anyway.
                if (c == '?') {
                    out += "\\?";
                } else {
                    out.push_back(static_cast<char>(c));
                }
            } else {
                out += std::format("\\{:03o}", c);
            }
            break;
        }
    }
    out += '"';
    return out;
}

namespace {

template <typename T> std::string floatLiteral(T value, const char *typeName, const char *suffix) {
    if (std::isnan(value)) {
        return std::format("std::numeric_limits<{}>::quiet_NaN()", typeName);
    }
    if (std::isinf(value)) {
        return std::format(
            "{}std::numeric_limits<{}>::infinity()",
            value < 0 ? "-" : "",
            typeName);
    }
    std::array<char, 64> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text(buffer.data(), ec == std::errc() ? end : buffer.data());
    // Shortest digits may have neither a point nor an exponent ("3"): make it a float literal.
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    return text + suffix;
}

} // namespace

std::string cppFloatLiteral(double value) { return floatLiteral(value, "double", ""); }
std::string cppFloatLiteral(float value) { return floatLiteral(value, "float", "f"); }

std::string cppIntLiteral(int64_t value) {
    if (value == std::numeric_limits<int64_t>::min()) {
        // -9223372036854775808 is a negation of a literal that does not fit.
        return "std::numeric_limits<int64_t>::min()";
    }
    // A bare literal: the helpers of the standalone runtime are overloaded so that an int64
    // operand next to a literal selects the int64 form (camel/standalone/num.h).
    return std::to_string(value);
}

std::string cppIntLiteral(int32_t value) {
    if (value == std::numeric_limits<int32_t>::min()) {
        return "std::numeric_limits<int32_t>::min()";
    }
    return std::to_string(value);
}

std::string cppIdentifier(std::string_view name) {
    static const std::set<std::string, std::less<>> kKeywords = {
        "alignas",   "alignof",   "and",        "and_eq",      "asm",          "auto",
        "bitand",    "bitor",     "bool",       "break",       "case",         "catch",
        "char",      "char8_t",   "char16_t",   "char32_t",    "class",        "compl",
        "concept",   "const",     "consteval",  "constexpr",   "constinit",    "const_cast",
        "continue",  "co_await",  "co_return",  "co_yield",    "decltype",     "default",
        "delete",    "do",        "double",     "dynamic_cast", "else",        "enum",
        "explicit",  "export",    "extern",     "false",       "float",        "for",
        "friend",    "goto",      "if",         "inline",      "int",          "long",
        "mutable",   "namespace", "new",        "noexcept",    "not",          "not_eq",
        "nullptr",   "operator",  "or",         "or_eq",       "private",      "protected",
        "public",    "register",  "reinterpret_cast", "requires", "return",    "short",
        "signed",    "sizeof",    "static",     "static_assert", "static_cast", "struct",
        "switch",    "template",  "this",       "thread_local", "throw",       "true",
        "try",       "typedef",   "typeid",     "typename",    "union",        "unsigned",
        "using",     "virtual",   "void",       "volatile",    "wchar_t",      "while",
        "xor",       "xor_eq",    "main",       "rt",          "std",          "depth",
    };
    std::string out;
    out.reserve(name.size());
    bool lastUnderscore = false;
    for (unsigned char c : name) {
        const bool keep = std::isalnum(c) != 0;
        if (keep) {
            out.push_back(static_cast<char>(c));
            lastUnderscore = false;
        } else if (!lastUnderscore && !out.empty()) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "value";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    // A double underscore anywhere, or an underscore and a capital at the start, is reserved.
    if (kKeywords.contains(out) || (out.size() > 1 && out[0] == '_' && std::isupper(out[1]))) {
        out += '_';
    }
    return out;
}

std::string NameScope::unique(const std::string &base) {
    if (used_.insert(base).second) {
        return base;
    }
    for (size_t n = 2;; ++n) {
        std::string candidate = std::format("{}_{}", base, n);
        if (used_.insert(candidate).second) {
            return candidate;
        }
    }
}

std::string StringPool::nameFor(std::string_view text) {
    if (auto it = names_.find(text); it != names_.end()) {
        return it->second;
    }
    // A name that says what the text is, when the text is a short word or two.
    std::string hint;
    for (unsigned char c : text.substr(0, 24)) {
        hint.push_back(std::isalnum(c) != 0 ? static_cast<char>(std::tolower(c)) : '_');
    }
    std::string base = "str";
    if (const std::string word = cppIdentifier(hint); hint.find_first_not_of('_') != std::string::npos &&
                                                       word != "value") {
        base += "_" + word;
    }
    std::string name = scope_.unique(base);
    names_.emplace(std::string(text), name);
    order_.emplace_back(name, std::string(text));
    return name;
}

std::string StringPool::definitions() const {
    std::string out;
    for (const auto &[name, text] : order_) {
        out += std::format(
            "const rt::Str {}{{std::string_view({}, {})}};\n",
            name,
            cppStringLiteral(text),
            text.size());
    }
    return out;
}

std::optional<std::string> cppConstant(slot_t slot, const Type *type, StringPool &strings) {
    if (type == nullptr) {
        return std::nullopt;
    }
    using camel::core::rtdata::fromSlot;
    switch (type->code()) {
    case TypeCode::Int32:
        return cppIntLiteral(fromSlot<int32_t>(slot));
    case TypeCode::Int64:
        return cppIntLiteral(fromSlot<int64_t>(slot));
    case TypeCode::Float32:
        return cppFloatLiteral(fromSlot<float>(slot));
    case TypeCode::Float64:
        return cppFloatLiteral(fromSlot<double>(slot));
    case TypeCode::Bool:
        return fromSlot<bool>(slot) ? "true" : "false";
    case TypeCode::String: {
        const auto *str = fromSlot<String *>(slot);
        return strings.nameFor(str != nullptr ? str->view() : std::string_view());
    }
    default:
        return std::nullopt;
    }
}

} // namespace camel::passes::cpp
