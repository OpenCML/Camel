// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The text of values, printing, and `format` in the programs std::cpp writes.
 *
 * The text of a value is what `println`, `string(x)` and a `{}` of `format` write for it
 * (camel/core/rtdata/base.h, printSlot): a float has one text, the shortest that reads back as
 * the same number (camel::utils::floatText, the runtime's own function).
 *
 * `format` follows the runtime (src/builtin/operators/str.cpp, float_format.h), which hands the
 * string to the fmt library: an integer is formatted as an integer, a float as a float (a
 * placeholder without a specification writes the float's text), a complex number as the two
 * numbers it is (src/builtin/operators/complex_format.h: the number part of the specification
 * writes each part, the fill, alignment and width lay out the whole text), and every other value
 * as its text. What fmt does with a specification ([[fill]align][sign][#][0][width][.precision][type])
 * is reproduced here for the forms a Camel program can write; a format string fmt rejects is
 * FormatFailed at the operation's site (values: the format string, what is wrong).
 *
 * `println` with a format string ends with one line break and `print` with one with none, as
 * without a format. (Both used to write one more, copied from the interpreter, whose defect it
 * was: design principles 10.6.)
 */

#pragma once

#include "camel/standalone/array.h"
#include "camel/standalone/complex.h"
#include "camel/standalone/core.h"
#include "camel/standalone/fn.h"
#include "camel/standalone/half.h"
#include "camel/standalone/opt.h"
#include "camel/standalone/str.h"
#include "camel/utils/format_spec.h"
#include "camel/utils/number_text.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace camel::standalone {

// ---- The text of a value ----

inline void appendText(std::string &out, int64_t v) { out += std::to_string(v); }
inline void appendText(std::string &out, int32_t v) { out += std::to_string(v); }
inline void appendText(std::string &out, double v) { out += camel::utils::floatText(v); }
inline void appendText(std::string &out, float v) { out += camel::utils::floatText(v); }
inline void appendText(std::string &out, bool v) { out += v ? "true" : "false"; }
inline void appendText(std::string &out, const Str &v) { out += v.view(); }
inline void appendText(std::string &out, const Unit &) { out += "null"; }

/// A bf16 or an f16: its own shortest text (half.h, the one float text rule), so a value prints
/// the same under `println`, `string(x)` and a `{}` of `format`.
template <typename Format> void appendText(std::string &out, Half<Format> v) {
    out += halfText(v);
}

/// One dimension of a value that has a shape: the elements from `offset`, then, for a deeper
/// dimension, one bracket group per extent. The rows are what the interpreter prints for a value of
/// several dimensions (`[[1.0, 2.0], [3.0, 4.0]]`), and the flat block underneath is the same run.
template <typename T>
void appendShapedText(std::string &out, const Array<T> &xs, std::size_t offset, std::size_t from) {
    const std::size_t extent = xs.extent(from);
    out += '[';
    if (from + 1 >= xs.rank()) {
        for (std::size_t i = 0; i < extent; ++i) {
            if (i > 0) {
                out += ", ";
            }
            appendText(out, xs[offset + i]);
        }
    } else {
        std::size_t inner = 1;
        for (std::size_t i = from + 1; i < xs.rank(); ++i) {
            inner *= xs.extent(i);
        }
        for (std::size_t i = 0; i < extent; ++i) {
            if (i > 0) {
                out += ", ";
            }
            appendShapedText(out, xs, offset + i * inner, from + 1);
        }
    }
    out += ']';
}

/// `[a, b, c]`, and `[[1.0, 2.0], [3.0, 4.0]]` for a value that states a shape of several
/// dimensions.
template <typename T> void appendText(std::string &out, const Array<T> &xs) {
    if (xs.rank() >= 2) {
        appendShapedText(out, xs, 0, 0);
        return;
    }
    out += '[';
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        appendText(out, xs[i]);
    }
    out += ']';
}

/// The payload, or `null`.
template <typename T> void appendText(std::string &out, const Opt<T> &v) {
    if (v.hasValue()) {
        appendText(out, v.value());
    } else {
        out += "null";
    }
}

/// The payload, or `error Kind: message` (camel/core/rtdata/tagged.cpp, printTaggedSlots).
template <typename T> void appendText(std::string &out, const Fallible<T> &v) {
    if (v.failed()) {
        out += "error ";
        out += v.error().kind.view();
        out += ": ";
        out += v.error().message.view();
    } else {
        appendText(out, v.value());
    }
}

/// The error structure as a struct: `{ kind: K, message: M, location: L }`.
inline void appendText(std::string &out, const Error &e) {
    out += "{ kind: ";
    out += e.kind.view();
    out += ", message: ";
    out += e.message.view();
    out += ", location: ";
    out += e.location.view();
    out += " }";
}

/// A function value as the runtime writes it: `Function(graph=f, tupleSlots=0)`.
template <typename S> void appendText(std::string &out, const Fn<S> &f) { out += f.text(); }

/// `string(x)` (:op/itos, :op/ltos, :op/ftos, :op/dtos, :op/atos): the text of `x`.
template <typename T> Str toText(const T &value) {
    std::string out;
    appendText(out, value);
    return Str(out);
}

// ---- println and print (:io/println, :io/print) ----

namespace detail {

template <typename... Args> std::string joined(const Args &...args) {
    std::string out;
    bool first = true;
    (
        [&] {
            if (!first) {
                out += ' ';
            }
            first = false;
            appendText(out, args);
        }(),
        ...);
    return out;
}

} // namespace detail

/// `println(a, b, ...)`: the texts separated by blanks, then a line break. Gives `a`
/// (design principles 3.1: an effect returns a value, here its first argument).
template <typename First, typename... Rest> First println(const First &first, const Rest &...rest) {
    write(detail::joined(first, rest...) + "\n");
    return first;
}
inline void println() { write("\n"); }

/// `print(a, b, ...)`: as println without the line break.
template <typename First, typename... Rest> First print(const First &first, const Rest &...rest) {
    write(detail::joined(first, rest...));
    return first;
}
inline void print() {}

// ---- format (:str/format) ----

namespace detail {

/// One argument of `format`, classified as the runtime hands it to fmt.
struct FormatArg {
    enum class Kind { Integer, Float32, Float64, Half, C64, C128, Text, Array } kind;
    int64_t integer  = 0;
    double real      = 0;
    double imaginary = 0;
    std::string text;
    /// An array whose element is a number: its text under a specification, written element by
    /// element. Empty for every other argument, and for an array whose element takes no
    /// specification of its own.
    std::function<std::string(std::string_view)> underSpec;
};

inline FormatArg formatArg(int64_t v) { return {FormatArg::Kind::Integer, v, 0, 0, {}}; }
inline FormatArg formatArg(int32_t v) { return {FormatArg::Kind::Integer, v, 0, 0, {}}; }
inline FormatArg formatArg(double v) { return {FormatArg::Kind::Float64, 0, v, 0, {}}; }
inline FormatArg formatArg(float v) { return {FormatArg::Kind::Float32, 0, v, 0, {}}; }
/// A bf16 or an f16: a number, so a specification writes it as it writes an f32 of the widened
/// value; a placeholder without one writes the text of its own type (the runtime's rule, so
/// `{}` and `println` agree), which `text` already holds.
template <typename Format> FormatArg formatArg(Half<Format> v) {
    FormatArg arg{FormatArg::Kind::Half, 0, v.toFloat(), 0, {}};
    appendText(arg.text, v);
    return arg;
}
inline FormatArg formatArg(std::complex<float> v) {
    return {FormatArg::Kind::C64, 0, v.real(), v.imag(), {}};
}
inline FormatArg formatArg(std::complex<double> v) {
    return {FormatArg::Kind::C128, 0, v.real(), v.imag(), {}};
}
template <typename T> FormatArg formatArg(const T &v) {
    FormatArg arg{FormatArg::Kind::Text, 0, 0, 0, {}};
    appendText(arg.text, v);
    return arg;
}

/// A format specification: [[fill]align][sign][#][0][width][.precision][type].
struct Spec {
    std::string fill = " ";
    char align       = '\0'; // '<', '>', '^' or none
    char sign        = '-';
    bool alternate   = false;
    bool zeroPad     = false;
    int width        = 0;
    int precision    = -1;
    char type        = '\0';
};

/// A format string fmt would reject: the text of its message.
struct FormatError {
    std::string what;
};

inline std::size_t codePoints(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char c : s) {
        n += (c & 0xC0) != 0x80;
    }
    return n;
}

inline std::string padded(std::string body, const Spec &spec, char defaultAlign) {
    const std::size_t width = codePoints(body);
    if (spec.width <= 0 || width >= static_cast<std::size_t>(spec.width)) {
        return body;
    }
    const std::size_t missing = static_cast<std::size_t>(spec.width) - width;
    const char align          = spec.align != '\0' ? spec.align : defaultAlign;
    std::size_t before        = 0;
    if (align == '>') {
        before = missing;
    } else if (align == '^') {
        before = missing / 2;
    }
    std::string out;
    for (std::size_t i = 0; i < before; ++i) {
        out += spec.fill;
    }
    out += body;
    for (std::size_t i = before; i < missing; ++i) {
        out += spec.fill;
    }
    return out;
}

/// `body` is a number's digits without a sign; `negative` its sign. Applies sign, `#` prefix
/// and zero padding (which goes between the sign and the digits, and only without an
/// explicit alignment), then the alignment.
inline std::string numeric(std::string body, bool negative, std::string_view prefix,
                           const Spec &spec) {
    std::string head = negative ? "-" : (spec.sign == '+' ? "+" : (spec.sign == ' ' ? " " : ""));
    head += prefix;
    if (spec.zeroPad && spec.align == '\0' && spec.width > 0) {
        const std::size_t have = head.size() + body.size();
        if (have < static_cast<std::size_t>(spec.width)) {
            body.insert(0, static_cast<std::size_t>(spec.width) - have, '0');
        }
        return head + body;
    }
    return padded(head + body, spec, '>');
}

inline std::string formatInteger(int64_t value, const Spec &spec) {
    if (spec.precision >= 0) {
        throw FormatError{"precision not allowed for this argument type"};
    }
    const bool negative = value < 0;
    const uint64_t magnitude =
        negative ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    int base                = 10;
    std::string_view prefix = "";
    switch (spec.type) {
    case '\0':
    case 'd':
        break;
    case 'x':
        base   = 16;
        prefix = spec.alternate ? "0x" : "";
        break;
    case 'X':
        base   = 16;
        prefix = spec.alternate ? "0X" : "";
        break;
    case 'b':
        base   = 2;
        prefix = spec.alternate ? "0b" : "";
        break;
    case 'B':
        base   = 2;
        prefix = spec.alternate ? "0B" : "";
        break;
    case 'o':
        base   = 8;
        prefix = spec.alternate && magnitude != 0 ? "0" : "";
        break;
    case 'c':
        return padded(std::string(1, static_cast<char>(value)), spec, '<');
    default:
        throw FormatError{"invalid format specifier"};
    }
    char digits[72];
    auto [end, ec] = std::to_chars(digits, digits + sizeof(digits), magnitude, base);
    std::string body(digits, end);
    if (spec.type == 'X' || spec.type == 'B') {
        for (char &c : body) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return numeric(std::move(body), negative, prefix, spec);
}

/// fmt's float without a type: the shortest digits that read back, laid out without an
/// exponent when the decimal exponent is from -4 to 15, and without a forced ".0".
template <typename T> std::string shortestFloat(T value) {
    std::string text = camel::utils::floatText(value);
    // floatText writes "2.0" where fmt writes "2"; everything else is the same layout.
    if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0 &&
        text.find('e') == std::string::npos) {
        text.resize(text.size() - 2);
    }
    return text;
}

template <typename T> std::string formatFloat(T value, const Spec &spec) {
    const bool negative = std::signbit(value) && !std::isnan(value);
    const T magnitude   = negative ? -value : value;
    std::string body;
    if (std::isnan(magnitude) || std::isinf(magnitude)) {
        const bool upper = spec.type == 'F' || spec.type == 'E' || spec.type == 'G';
        body = std::isnan(magnitude) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        Spec plain    = spec;
        plain.zeroPad = false; // fmt does not pad infinity and NaN with zeros
        return numeric(std::move(body), negative, "", plain);
    }
    if (spec.type == '\0' && spec.precision < 0) {
        body = shortestFloat(magnitude);
        if (spec.alternate && body.find('.') == std::string::npos &&
            body.find('e') == std::string::npos) {
            body += '.';
        }
        return numeric(std::move(body), negative, "", spec);
    }
    char conversion    = spec.type;
    double v           = static_cast<double>(magnitude);
    std::string suffix = "";
    switch (spec.type) {
    case '\0':
        conversion = 'g';
        break;
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
        break;
    case '%':
        conversion = 'f';
        v *= 100.0;
        suffix = "%";
        break;
    default:
        throw FormatError{"invalid format specifier"};
    }
    std::string pattern = "%";
    if (spec.alternate) {
        pattern += '#';
    }
    if (spec.precision >= 0) {
        pattern += "." + std::to_string(spec.precision);
    }
    pattern += conversion;
    const int n = std::snprintf(nullptr, 0, pattern.c_str(), v);
    body.resize(static_cast<std::size_t>(n) + 1);
    std::snprintf(body.data(), body.size(), pattern.c_str(), v);
    body.resize(static_cast<std::size_t>(n));
    return numeric(body + suffix, negative, "", spec);
}

inline std::string formatText(const std::string &text, const Spec &spec) {
    if (spec.type != '\0' && spec.type != 's') {
        throw FormatError{"invalid format specifier"};
    }
    if (spec.sign != '-' || spec.alternate || spec.zeroPad) {
        throw FormatError{"invalid format specifier"};
    }
    std::string body = text;
    if (spec.precision >= 0) {
        // The precision of a string is the number of characters (code points) kept.
        std::size_t kept = 0, at = 0;
        while (at < body.size() && kept < static_cast<std::size_t>(spec.precision)) {
            ++at;
            while (at < body.size() && (static_cast<unsigned char>(body[at]) & 0xC0) == 0x80) {
                ++at;
            }
            ++kept;
        }
        body.resize(at);
    }
    return padded(std::move(body), spec, '<');
}

inline int parseNumber(std::string_view s, std::size_t &i) {
    int value = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        value = value * 10 + (s[i] - '0');
        ++i;
    }
    return value;
}

inline Spec parseSpec(std::string_view s) {
    Spec spec;
    std::size_t i = 0;
    auto isAlign  = [](char c) { return c == '<' || c == '>' || c == '^'; };
    // A fill is one code point followed by an alignment.
    std::size_t fillEnd = 0;
    if (!s.empty()) {
        fillEnd = 1;
        while (fillEnd < s.size() && (static_cast<unsigned char>(s[fillEnd]) & 0xC0) == 0x80) {
            ++fillEnd;
        }
    }
    if (fillEnd < s.size() && isAlign(s[fillEnd]) && s[0] != '{' && s[0] != '}') {
        spec.fill  = std::string(s.substr(0, fillEnd));
        spec.align = s[fillEnd];
        i          = fillEnd + 1;
    } else if (!s.empty() && isAlign(s[0])) {
        spec.align = s[0];
        i          = 1;
    }
    if (i < s.size() && (s[i] == '+' || s[i] == '-' || s[i] == ' ')) {
        spec.sign = s[i++];
    }
    if (i < s.size() && s[i] == '#') {
        spec.alternate = true;
        ++i;
    }
    if (i < s.size() && s[i] == '0') {
        spec.zeroPad = true;
        ++i;
    }
    if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        spec.width = parseNumber(s, i);
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (i >= s.size() || s[i] < '0' || s[i] > '9') {
            throw FormatError{"missing precision specifier"};
        }
        spec.precision = parseNumber(s, i);
    }
    if (i < s.size() && s[i] == 'L') {
        ++i;
    }
    if (i < s.size()) {
        spec.type = s[i++];
    }
    if (i != s.size()) {
        throw FormatError{"invalid format specifier"};
    }
    return spec;
}

/// A complex number under one specification (`complex_format.h` in the interpreter): the
/// precision, the type and the `#` flag write each part, the fill, the alignment and the width
/// lay out the whole text, and the sign of the imaginary part is the separator. Zero padding and
/// the `=` alignment are errors, as they are for a complex number in Python.
template <typename T>
std::string formatComplex(T real, T imaginary, std::string_view specText, bool hasSpec) {
    if (!hasSpec || specText.empty()) {
        return camel::utils::complexText<T>(real, imaginary);
    }
    const camel::utils::NumberFormatSpec parts =
        camel::utils::splitNumberFormatSpec(specText);
    if (parts.zeroPadded) {
        throw FormatError{"Zero padding is not allowed in complex format specifier"};
    }
    if (parts.equalAlign) {
        throw FormatError{"'=' alignment is not allowed in complex format specifier"};
    }
    const std::string realText = formatFloat(real, parseSpec(parts.number));
    std::string magnitudeSpec  = parts.number;
    if (!magnitudeSpec.empty() &&
        (magnitudeSpec[0] == '+' || magnitudeSpec[0] == '-' || magnitudeSpec[0] == ' ')) {
        magnitudeSpec.erase(0, 1);
    }
    const bool negative     = !std::isnan(imaginary) && std::signbit(imaginary);
    const T magnitude       = negative ? -imaginary : imaginary;
    std::string text        = realText;
    text += negative ? '-' : '+';
    text += formatFloat(magnitude, parseSpec(magnitudeSpec));
    text += 'i';
    return camel::utils::padFormatText(text, parts.fill, parts.align, parts.width);
}

inline std::string formatOne(const FormatArg &arg, std::string_view specText, bool hasSpec) {
    switch (arg.kind) {
    case FormatArg::Kind::Integer:
        return formatInteger(arg.integer, parseSpec(specText));
    case FormatArg::Kind::Float32:
        return hasSpec ? formatFloat(static_cast<float>(arg.real), parseSpec(specText))
                       : camel::utils::floatText(static_cast<float>(arg.real));
    case FormatArg::Kind::Half:
        // No specification -- `{}` and the empty `{:}` alike -- is the text of the element's own
        // type; any other specification is the widened value as an f32, which is what the runtime
        // does with a half under a format (`{:10}` of a bf16 0.1 is `0.100097656`, not `0.1`).
        return hasSpec && !specText.empty()
                   ? formatFloat(static_cast<float>(arg.real), parseSpec(specText))
                   : arg.text;
    case FormatArg::Kind::Float64:
        return hasSpec ? formatFloat(arg.real, parseSpec(specText))
                       : camel::utils::floatText(arg.real);
    case FormatArg::Kind::C64:
        return formatComplex(
            static_cast<float>(arg.real), static_cast<float>(arg.imaginary), specText, hasSpec);
    case FormatArg::Kind::C128:
        return formatComplex(arg.real, arg.imaginary, specText, hasSpec);
    case FormatArg::Kind::Array:
        // A numeric array under a specification is written element by element, its brackets and
        // its `, ` its own; an array whose element takes no specification of its own is text,
        // and the specification lays out that text (`string[]` with `{:>8}`).
        if (hasSpec && arg.underSpec) {
            return arg.underSpec(specText);
        }
        return formatText(arg.text, parseSpec(specText));
    case FormatArg::Kind::Text:
    default:
        return formatText(arg.text, parseSpec(specText));
    }
}

/// Whether one specification acts on each element of a `T` on its own, as the interpreter
/// decides it (`isNumericElementType` in src/builtin/operators/str.cpp): the numbers. An array of
/// arrays is not one of them -- its element is an array, and the specification lays out the whole
/// text, exactly as `string[]` does.
template <typename T> struct SpecPerElement : std::false_type {};
template <> struct SpecPerElement<int32_t> : std::true_type {};
template <> struct SpecPerElement<int64_t> : std::true_type {};
template <> struct SpecPerElement<float> : std::true_type {};
template <> struct SpecPerElement<double> : std::true_type {};
template <typename Format> struct SpecPerElement<Half<Format>> : std::true_type {};

/// The text of a numeric array with `spec` applied to each element -- `'{:.4f}'->format<xs>` is
/// `[0.7692, 0.7808]`: the specification acts on every element on its own, while the brackets
/// and the `, ` between elements are the array's own form (design principles 7.7).
template <typename T>
void appendArrayUnder(std::string &out, const Array<T> &xs, std::string_view spec) {
    out += '[';
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += formatOne(formatArg(xs[i]), spec, true);
    }
    out += ']';
}

/// `formatArg` for an array: the value's own text, and -- when its element is a number -- the
/// text the specification writes element by element.
template <typename T> FormatArg formatArg(const Array<T> &xs) {
    FormatArg arg{FormatArg::Kind::Array, 0, 0, 0, {}};
    appendText(arg.text, xs);
    if constexpr (SpecPerElement<T>::value) {
        arg.underSpec = [xs](std::string_view spec) {
            std::string out;
            appendArrayUnder(out, xs, spec);
            return out;
        };
    }
    return arg;
}

/// fmt::vformat over the classified arguments.
inline std::string vformat(std::string_view pattern, const std::vector<FormatArg> &args) {
    std::string out;
    std::size_t next = 0;
    enum class Indexing { None, Automatic, Manual } indexing = Indexing::None;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '}') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '}') {
                out += '}';
                ++i;
                continue;
            }
            throw FormatError{"unmatched '}' in format string"};
        }
        if (c != '{') {
            out += c;
            continue;
        }
        if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
            out += '{';
            ++i;
            continue;
        }
        const std::size_t close = pattern.find('}', i + 1);
        if (close == std::string_view::npos) {
            throw FormatError{"invalid format string"};
        }
        std::string_view field = pattern.substr(i + 1, close - i - 1);
        const std::size_t colon = field.find(':');
        std::string_view id     = field.substr(0, colon);
        std::string_view spec =
            colon == std::string_view::npos ? std::string_view() : field.substr(colon + 1);
        std::size_t index = 0;
        if (id.empty()) {
            if (indexing == Indexing::Manual) {
                throw FormatError{"cannot switch from manual to automatic argument indexing"};
            }
            indexing = Indexing::Automatic;
            index    = next++;
        } else {
            if (indexing == Indexing::Automatic) {
                throw FormatError{"cannot switch from automatic to manual argument indexing"};
            }
            indexing        = Indexing::Manual;
            std::size_t pos = 0;
            if (id[0] < '0' || id[0] > '9') {
                throw FormatError{"invalid format string"};
            }
            index = static_cast<std::size_t>(parseNumber(id, pos));
            if (pos != id.size()) {
                throw FormatError{"invalid format string"};
            }
        }
        if (index >= args.size()) {
            throw FormatError{"argument not found"};
        }
        out += formatOne(args[index], spec, colon != std::string_view::npos);
        i = close;
    }
    return out;
}

template <typename... Args> std::string formatted(const Str &pattern, const Site &site,
                                                  const Args &...args) {
    try {
        return vformat(pattern.view(), std::vector<FormatArg>{formatArg(args)...});
    } catch (const FormatError &e) {
        fault(site, {pattern.str(), e.what});
    }
}

} // namespace detail

/// `pattern->format<a, b, ...>` (:str/format).
template <typename... Args> Str format(const Str &pattern, const Site &site, const Args &...args) {
    return Str(detail::formatted(pattern, site, args...));
}

/// `println<pattern>(a, b, ...)`: the formatted text and a line break. Gives `a`.
template <typename First, typename... Rest>
First printlnFormatted(const Str &pattern, const Site &site, const First &first,
                       const Rest &...rest) {
    write(detail::formatted(pattern, site, first, rest...) + "\n");
    return first;
}
inline void printlnFormatted(const Str &pattern, const Site &site) {
    write(detail::formatted(pattern, site) + "\n");
}

/// `print<pattern>(a, b, ...)`: the formatted text, without a line break. Gives `a`.
template <typename First, typename... Rest>
First printFormatted(const Str &pattern, const Site &site, const First &first,
                     const Rest &...rest) {
    write(detail::formatted(pattern, site, first, rest...));
    return first;
}
inline void printFormatted(const Str &pattern, const Site &site) {
    write(detail::formatted(pattern, site));
}

} // namespace camel::standalone
