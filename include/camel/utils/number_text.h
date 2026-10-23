// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Numbers and text, and numbers between types: the rules of design principles 5.1 ("数值"),
 * in one place so that every conversion function, every back end and every printer applies the
 * same ones.
 *
 *   text -> number   the whole string must be one number: no blanks, nothing after it.
 *   float -> text    one rule: the shortest text that reads back as the same number, where "the
 *                    same number" is an element of the value's own type -- so a bf16 or an f16
 *                    is measured against its own rounding, not against f32 (halfText).
 *   number -> number a value that does not fit the target type is a fault
 *                    (camel/core/numeric_convert.h raises it; here are the range tests).
 */

#pragma once

#include <cerrno>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace camel::utils {

/// True for the two reduced-precision floating types (bf16, f16). Their text is found by the same
/// rule as any other float's -- the shortest that reads back as the same value -- but "the same
/// value" is an element of their own type, so the shortest digits are found against that type's
/// rounding (camel/core/rtdata/half.h). The declaration lives here and its specializations come
/// with the two types, so that this file needs nothing from the runtime's half types.
template <typename T> struct IsHalfFloat : std::false_type {};
template <typename T> inline constexpr bool is_half_float_v = IsHalfFloat<T>::value;

/// Reading a text as a number: it is one number of the language's grammar and the type holds
/// it, or it is not a number at all, or it is a number the type cannot hold (overflow or
/// underflow). The three refusals are different causes and are reported differently.
/// `Underflow` is a nonzero number too small to tell from zero at the target width (H15i): it
/// is refused like an overflow, never read as zero, and the reason can say which it is.
/// `OutOfRangeInteger` is a number written as an integer token whose value does not fit an
/// int64 (the widest integer a literal has): a float entry argument is refused with the
/// integer's range rather than the float's, which would be a false statement about the text.
enum class NumberTextStatus { Ok, NotANumber, Underflow, OutOfRange, OutOfRangeInteger };

/// The range of the integer type T as a diagnostic writes it (`-2147483648 to 2147483647`).
template <std::signed_integral T> std::string integerRangeText() {
    return std::to_string(std::numeric_limits<T>::min()) + " to " +
           std::to_string(std::numeric_limits<T>::max());
}

/// The range of the floating-point type T as a diagnostic writes it (`at most about 3.4e38 in
/// magnitude`); the same words name the range of a literal (literal::rangeText).
template <std::floating_point T> std::string_view floatRangeText() {
    if constexpr (std::is_same_v<T, float>) {
        return "at most about 3.4e38 in magnitude";
    } else {
        return "at most about 1.8e308 in magnitude";
    }
}

/// The range of a bf16 or an f16, in the same words (`at most about 3.4e38 in magnitude` for
/// bf16, the range of f32 with a shorter significand; `at most 65504 in magnitude` for f16).
/// A reduced-precision type is a floating type with a range of its own, so a literal that does
/// not fit it is reported with that range, not with f32's (design principles 7.3).
inline std::string_view halfRangeText(bool isBFloat16) {
    return isBFloat16 ? std::string_view("at most about 3.4e38 in magnitude")
                      : std::string_view("at most 65504 in magnitude");
}

/// The range of one part of a complex number whose parts are of type T (`each part at most
/// about 3.4e38 in magnitude`).
template <std::floating_point T> std::string complexRangeText() {
    return std::string("each part ") + std::string(floatRangeText<T>());
}

/// How a complex number is written, in the words an entry argument's refusal uses: the two
/// parts and the sign of the imaginary one, an imaginary-only form, or a real number (which is
/// its real part, design principles 5.1).
inline constexpr std::string_view complexNumberTextHelp =
    "a complex number (write 1+2i, -2i, 0.5-0.25i or a real number such as 1.5)";

namespace number_text_detail {

inline bool isDecimalDigit(char c) { return c >= '0' && c <= '9'; }

inline bool allDecimalDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (!isDecimalDigit(c)) {
            return false;
        }
    }
    return true;
}

inline bool allHexDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        const bool digit = isDecimalDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!digit) {
            return false;
        }
    }
    return true;
}

inline bool allBinaryDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c != '0' && c != '1') {
            return false;
        }
    }
    return true;
}

inline bool allOctalDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '7') {
            return false;
        }
    }
    return true;
}

inline unsigned digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a') + 10;
    }
    return static_cast<unsigned>(c - 'A') + 10;
}

/// An optional sign, then the body of a number token (language reference 2.5.1, 2.5.2). A sign
/// inside the body (a second leading sign, or an exponent without digits) is left to the form
/// check.
inline bool splitNumberSign(std::string_view &text, bool &negative) {
    negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
            return false;
        }
    }
    return true;
}

/// The body of a number token read as an integer (reference 2.5.1): decimal digits, `0x` and
/// hexadecimal digits, `0b` and binary digits, or `0o` and octal digits. Nothing else; `digits`
/// is written without the radix prefix.
///
/// A leading `0` is decimal here, not octal (H15i): `077` is the number 77. The source lexer
/// refuses a leading-zero digit run before a number token reaches this function, so a literal
/// cannot be written that way; an entry argument can, and so can the decimal text of a
/// conversion function (data text, language reference 2.5.2). Octal in source is written
/// `0o77`, which the source lexer and the entry reader accept; `parseWholeInteger` reads decimal
/// digits only, so `int('0o77')` is not an integer text.
struct IntegerParts {
    unsigned radix = 10;
    std::string_view digits;
};

inline bool integerParts(std::string_view body, IntegerParts &parts) {
    if (body.empty()) {
        return false;
    }
    if (body.size() >= 2 && body[0] == '0' && body[1] == 'x') {
        if (!allHexDigits(body.substr(2))) {
            return false;
        }
        parts = {16, body.substr(2)};
        return true;
    }
    if (body.size() >= 2 && body[0] == '0' && body[1] == 'b') {
        if (!allBinaryDigits(body.substr(2))) {
            return false;
        }
        parts = {2, body.substr(2)};
        return true;
    }
    if (body.size() >= 2 && body[0] == '0' && body[1] == 'o') {
        if (!allOctalDigits(body.substr(2))) {
            return false;
        }
        parts = {8, body.substr(2)};
        return true;
    }
    if (!allDecimalDigits(body)) {
        return false;
    }
    parts = {10, body};
    return true;
}

/// The body of a real token (reference 2.5.2): digits, then either a point with digits after it
/// (and an optional exponent) or an exponent. `point` and `exponent` say which parts are there;
/// a body with neither is an integer token, not a real one. `integerDigits` is the length of the
/// digit run before the point or the exponent.
struct RealParts {
    bool point                = false;
    bool exponent             = false;
    std::size_t integerDigits = 0;
};

inline bool realParts(std::string_view body, RealParts &parts) {
    std::size_t i = 0;
    while (i < body.size() && isDecimalDigit(body[i])) {
        ++i;
    }
    if (i == 0) {
        return false;
    }
    parts               = {};
    parts.integerDigits = i;
    if (i < body.size() && body[i] == '.') {
        parts.point = true;
        ++i;
        const std::size_t fraction = i;
        while (i < body.size() && isDecimalDigit(body[i])) {
            ++i;
        }
        if (i == fraction) {
            return false;
        }
    }
    if (i < body.size() && (body[i] == 'e' || body[i] == 'E')) {
        parts.exponent = true;
        ++i;
        if (i < body.size() && (body[i] == '+' || body[i] == '-')) {
            ++i;
        }
        const std::size_t exponentDigits = i;
        while (i < body.size() && isDecimalDigit(body[i])) {
            ++i;
        }
        if (i == exponentDigits) {
            return false;
        }
    }
    return i == body.size();
}

/// The value of `digits` in `radix`, read as T; the text is known to be an integer token. A
/// magnitude the type cannot hold is OutOfRange, never wrapped around (design principles 5.1).
template <std::signed_integral T>
NumberTextStatus integerValue(std::string_view digits, unsigned radix, bool negative, T &out) {
    // The largest magnitude the type holds, as an unsigned value: for a negative number the
    // magnitude may be one more (the minimum has no positive counterpart).
    const uint64_t maximum = static_cast<uint64_t>(std::numeric_limits<T>::max());
    const uint64_t limit   = negative ? maximum + 1 : maximum;
    uint64_t magnitude     = 0;
    for (const char c : digits) {
        const uint64_t digit = digitValue(c);
        if (digit > limit || magnitude > (limit - digit) / radix) {
            return NumberTextStatus::OutOfRange;
        }
        magnitude = magnitude * radix + digit;
    }
    if (negative) {
        if (magnitude == maximum + 1) {
            out = std::numeric_limits<T>::min();
        } else {
            out = static_cast<T>(-static_cast<int64_t>(magnitude));
        }
    } else {
        out = static_cast<T>(magnitude);
    }
    return NumberTextStatus::Ok;
}

/// The value of a real body. A number too small to tell from zero is Underflow and a number too
/// large is OutOfRange; neither is read as a value (H15i). The two are told apart at T's own
/// width: 3.5e38 overflows a float but is a finite double, and 1e-9999 underflows both.
template <std::floating_point T>
NumberTextStatus realValue(std::string_view body, bool negative, T &out) {
    T value{};
    if constexpr (requires(const char *p, T &v) { std::from_chars(p, p, v); }) {
        const auto [end, ec] = std::from_chars(
            body.data(),
            body.data() + body.size(),
            value,
            std::chars_format::general);
        if (ec == std::errc::invalid_argument || end != body.data() + body.size()) {
            return NumberTextStatus::NotANumber;
        }
        if (ec == std::errc::result_out_of_range) {
            // from_chars says the same thing for a number too large for T and one too small to
            // tell from zero; a C conversion at T's own width tells the two apart without
            // losing the grammar, which the checks above have already pinned. It must be T's
            // width: 3.5e38 overflows a float but is a finite double, and reading it as a double
            // would call it an underflow.
            const std::string numeral(body);
            errno     = 0;
            char *end = nullptr;
            T spread  = T{};
            if constexpr (std::is_same_v<T, float>) {
                spread = std::strtof(numeral.c_str(), &end);
            } else {
                spread = std::strtod(numeral.c_str(), &end);
            }
            if (std::isinf(spread)) {
                return NumberTextStatus::OutOfRange;
            }
            return NumberTextStatus::Underflow;
        }
    } else {
        // libc++ before version 20 has no from_chars for floating point. The characters were
        // checked by realParts; the C library reads the decimal point of the C locale, which
        // Camel never changes from ".".
        const std::string numeral(body);
        char *end = nullptr;
        errno     = 0;
        value     = static_cast<T>(
            sizeof(T) <= sizeof(float) ? std::strtof(numeral.c_str(), &end)
                                       : std::strtod(numeral.c_str(), &end));
        if (end != numeral.c_str() + numeral.size()) {
            return NumberTextStatus::NotANumber;
        }
        if (errno == ERANGE && (std::isinf(value) || value == T{})) {
            return std::isinf(value) ? NumberTextStatus::OutOfRange : NumberTextStatus::Underflow;
        }
    }
    out = negative ? -value : value;
    return NumberTextStatus::Ok;
}

} // namespace number_text_detail

/// Reads `text` as one integer of type T for a conversion function: an optional sign ('-' or
/// '+'), then decimal digits, and nothing else (no blanks, no digit separators, no base prefix).
/// The entry reader and a source literal use parseNumberToken, which reads a whole number token
/// (the language's bases included).
template <std::signed_integral T>
NumberTextStatus parseWholeInteger(std::string_view text, T &out) {
    bool negative = false;
    if (!number_text_detail::splitNumberSign(text, negative)) {
        return NumberTextStatus::NotANumber;
    }
    if (!number_text_detail::allDecimalDigits(text)) {
        return NumberTextStatus::NotANumber;
    }
    return number_text_detail::integerValue(text, 10, negative, out);
}

/// Reads `text` as one floating-point number of type T for a conversion function: an optional
/// sign, then a decimal number with digits on both sides of its optional fraction and an
/// optional exponent (`1`, `1.5`, `1e-3`), or `inf` / `nan` as the language prints them.
/// Nothing else: no blanks, no hexadecimal form, no suffix. A number too small to tell from zero
/// is refused as out of range here (`ConversionOutOfRange`), the same rule the literal and the
/// entry argument apply (H15i); a conversion reports both overflow and underflow with one
/// diagnostic, so the distinction is folded into OutOfRange.
template <std::floating_point T> NumberTextStatus parseWholeFloat(std::string_view text, T &out) {
    bool negative = false;
    if (!number_text_detail::splitNumberSign(text, negative)) {
        return NumberTextStatus::NotANumber;
    }
    if (text.empty()) {
        return NumberTextStatus::NotANumber;
    }
    if (text == "inf") {
        out = negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
        return NumberTextStatus::Ok;
    }
    if (text == "nan") {
        out = std::numeric_limits<T>::quiet_NaN();
        return NumberTextStatus::Ok;
    }
    // The language writes a float as digits, an optional `.` followed by digits, and an optional
    // exponent (language reference 2.5.2: there must be digits on both sides of the point).
    // from_chars and strtod also accept `.5` and `1.`; reject them here, so that a number text
    // has the language's grammar rather than the C library's.
    number_text_detail::RealParts parts;
    if (!number_text_detail::realParts(text, parts)) {
        return NumberTextStatus::NotANumber;
    }
    const NumberTextStatus status = number_text_detail::realValue(text, negative, out);
    return status == NumberTextStatus::Underflow ? NumberTextStatus::OutOfRange : status;
}

/// Reads `text` as one whole number token of the language, the rule a source literal and an
/// entry argument share (reference 2.5.1, 2.5.2): an optional sign, then an integer token
/// (decimal, `0x` hexadecimal, `0b` binary, `0o` octal) or a real token (digits with a point and
/// digits after it, and/or an exponent), and nothing else. A real token too small to tell from
/// zero is Underflow and one too large is OutOfRange (H15i); neither is a value. For an integer
/// T only an integer token fits (`12`, `0x10`, `0o77`; `1.5` and `1e3` do not). A leading zero
/// is decimal (`077` is 77): the lexer keeps such a run out of a source literal, but an entry
/// argument and a conversion text read it, as data text (2.5.2).
template <std::signed_integral T> NumberTextStatus parseNumberToken(std::string_view text, T &out) {
    bool negative = false;
    if (!number_text_detail::splitNumberSign(text, negative)) {
        return NumberTextStatus::NotANumber;
    }
    number_text_detail::IntegerParts parts;
    if (!number_text_detail::integerParts(text, parts)) {
        return NumberTextStatus::NotANumber;
    }
    return number_text_detail::integerValue(parts.digits, parts.radix, negative, out);
}

template <std::floating_point T> NumberTextStatus parseNumberToken(std::string_view text, T &out) {
    bool negative = false;
    if (!number_text_detail::splitNumberSign(text, negative)) {
        return NumberTextStatus::NotANumber;
    }
    if (text.empty()) {
        return NumberTextStatus::NotANumber;
    }
    if (text == "inf") {
        out = negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
        return NumberTextStatus::Ok;
    }
    if (text == "nan") {
        out = std::numeric_limits<T>::quiet_NaN();
        return NumberTextStatus::Ok;
    }
    // An integer token first: `0o77` is octal 63, and `0xFF` is 255. A source literal is written
    // that way; a leading zero (`077`, `09`) is plain decimal data text, which the lexer refuses
    // in source but an entry argument and a conversion text read (H15i, reference 2.5.2).
    number_text_detail::IntegerParts integer;
    if (number_text_detail::integerParts(text, integer)) {
        int64_t value = 0;
        const NumberTextStatus status =
            number_text_detail::integerValue(integer.digits, integer.radix, negative, value);
        if (status != NumberTextStatus::Ok) {
            return NumberTextStatus::OutOfRangeInteger;
        }
        out = static_cast<T>(value);
        if (negative && value == 0) {
            // The text wrote a sign the integer value cannot carry: `-0` at a position of a real
            // type is the negative zero, and converting the integer zero would lose it (H15h).
            out = -out;
        }
        return NumberTextStatus::Ok;
    }
    // Then a real token: digits, an optional point, an optional exponent. A leading-zero digit
    // run with an exponent (`077e2`) is decimal data text, like `077`; the lexer refuses it in
    // source before a literal can be built from it.
    number_text_detail::RealParts real;
    if (!number_text_detail::realParts(text, real)) {
        return NumberTextStatus::NotANumber;
    }
    return number_text_detail::realValue(text, negative, out);
}

/// Whether `f32` reads the digits of `scientific` back as the f32 they were written from, by
/// parsing them and comparing the bits: the test the text of a reduced-precision type is held to
/// after its own rounding (below). The digits are correctly rounded by `to_chars`, so a text
/// inside the rounding interval of the value parses to exactly that value; a text outside it
/// parses to a neighbour and the bits differ.
inline bool scientificParsesBack(std::string_view scientific, float &parsed) {
    const auto [end, ec] =
        std::from_chars(scientific.data(), scientific.data() + scientific.size(), parsed);
    return ec == std::errc() && end == scientific.data() + scientific.size();
}

/// The shortest scientific text -- `[-]d[.ddd]e[+-]dd`, correctly rounded by `to_chars` -- whose
/// digits read back under `roundsBack` as the value they came from, for a value that is an
/// element of a type narrower than f32 (a bf16 or an f16; design principles 7.7, the ruling of
/// 2026-10-23, "reduced-precision-text"). `value` is the exact f32 widening of the element, so
/// its own digits are a fallback that always reads back, and the loop only takes fewer of them.
/// `roundsBack(token)` says whether one candidate is the same element of the narrow type.
template <typename RoundsBack>
std::string_view shortestHalfDigits(float value, RoundsBack roundsBack, std::string &storage) {
    char buffer[64];
    // Each count is rounded to that many significant decimal digits, so every candidate is the
    // nearest text of its length and the first length that reads back is the shortest one.
    for (int digitCount = 1; digitCount < 9; ++digitCount) {
        const auto [end, ec] = std::to_chars(
            buffer,
            buffer + sizeof(buffer),
            value,
            std::chars_format::scientific,
            digitCount);
        if (ec != std::errc()) {
            break;
        }
        const std::string_view candidate(buffer, static_cast<size_t>(end - buffer));
        if (roundsBack(candidate)) {
            // `to_chars` writes exactly `digitCount` significant digits, so a value that needs
            // fewer than that round-trips with a trailing zero -- `0.1 as bf16` reads back from
            // "1.0e-01" at one digit. Dropping those zeros is what makes the text shortest; the
            // exponent of the leading digit does not move, because only the last digit can be a
            // zero here.
            const size_t ePos = candidate.find('e');
            std::string_view mantissa = candidate.substr(0, ePos);
            size_t keep              = mantissa.size();
            while (keep > 1 && mantissa[keep - 1] == '0') {
                --keep;
            }
            if (keep > 1 && mantissa[keep - 1] == '.') {
                --keep;
            }
            if (keep == mantissa.size()) {
                return candidate;
            }
            storage.assign(mantissa.substr(0, keep));
            storage += candidate.substr(ePos);
            return storage;
        }
    }
    // No fewer digits read back; the widest form of the exact value does (it is the f32's own,
    // and the element widens to f32 without loss).
    const auto [end, ec] = std::to_chars(
        buffer,
        buffer + sizeof(buffer),
        value,
        std::chars_format::scientific,
        9);
    storage.assign(buffer, static_cast<size_t>(end - buffer));
    return storage;
}

/// One decimal text laid out as the language writes a float (below), from its digits and its
/// decimal exponent: the one layout, whatever the type the digits were found for.
inline std::string layoutFloatText(std::string_view digits, int exponent, bool negative) {
    std::string text = negative ? "-" : "";
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            text += "0.";
            text.append(static_cast<size_t>(-exponent - 1), '0');
            text += digits;
        } else {
            const size_t whole = static_cast<size_t>(exponent) + 1;
            if (digits.size() <= whole) {
                text += digits;
                text.append(whole - digits.size(), '0');
                text += ".0";
            } else {
                text.append(digits, 0, whole);
                text += '.';
                text.append(digits, whole, std::string::npos);
            }
        }
        return text;
    }
    text += digits.front();
    if (digits.size() > 1) {
        text += '.';
        text.append(digits, 1, std::string::npos);
    }
    text += 'e';
    text += exponent < 0 ? '-' : '+';
    const int magnitude = exponent < 0 ? -exponent : exponent;
    if (magnitude < 10) {
        text += '0';
    }
    text += std::to_string(magnitude);
    return text;
}

/// The text of a reduced-precision floating value, bf16 or f16: the same rule as for every other
/// float -- the shortest digits that read back as the same value (design principles 7.7) -- with
/// "the same value" an element of the value's own type. The element widens to f32 exactly, so the
/// digits are searched against that f32 and each candidate is held to the store rounding of the
/// type (round to nearest, ties to even: `roundsBack`); `0.1 as bf16` prints `0.1`, where the
/// f32 text of the same bits would print `0.100097656` (ruling 2026-10-23, "reduced-precision-text").
template <typename Half, typename RoundsBack>
std::string halfText(Half value, RoundsBack roundsBack) {
    const float widened = value.toFloat();
    if (std::isnan(widened)) {
        return "nan";
    }
    if (std::isinf(widened)) {
        return widened < 0 ? "-inf" : "inf";
    }
    if (widened == 0) {
        return std::signbit(widened) ? "-0.0" : "0.0";
    }
    std::string storage;
    std::string_view scientific = shortestHalfDigits(widened, roundsBack, storage);
    const bool negative         = scientific.front() == '-';
    if (negative) {
        scientific.remove_prefix(1);
    }
    const size_t ePos = scientific.find('e');
    std::string digits;
    for (const char c : scientific.substr(0, ePos)) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    int exponent = 0;
    {
        std::string_view expText = scientific.substr(ePos + 1);
        if (!expText.empty() && expText.front() == '+') {
            expText.remove_prefix(1);
        }
        std::from_chars(expText.data(), expText.data() + expText.size(), exponent);
    }
    return layoutFloatText(digits, exponent, negative);
}

/// The text of a floating-point number: the shortest digits that read back as the same number
/// (design principles 7.7), laid out so that the text is always recognisable as a float.
///
///   - a number whose decimal exponent is from -4 to 15 is written without an exponent, with
///     at least one digit after the point: `2.0`, `100000.0`, `0.1`, `0.0001`;
///   - any other is written with one: `1e+16`, `1.5e-07` (a sign and at least two digits);
///   - the zeros are `0.0` and `-0.0`, the infinities `inf` and `-inf`;
///   - every NaN is `nan`, never `-nan`: the sign of a NaN says nothing about a value.
///
/// The text of an `f32` is the shortest that reads back as that `f32` (`0.1`, not
/// `0.10000000149011612`); a bf16 or an f16 is the shortest that reads back as that element of
/// its own type, which is the same function with the digits measured against the narrower
/// rounding (`halfText` above; between f32 and f64 "the same number" is already the type's own).
/// parseWholeFloat reads every text this writes.
template <std::floating_point T>
    requires(!is_half_float_v<T>)
std::string floatText(T value) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    // Shortest round-trip digits in scientific form: [-]d[.ddd]e[+-]dd
    char buffer[64];
    const auto [end, ec] =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    if (ec != std::errc()) {
        return "nan";
    }
    std::string_view sci(buffer, static_cast<size_t>(end - buffer));
    const bool negative = !sci.empty() && sci.front() == '-';
    if (negative) {
        sci.remove_prefix(1);
    }
    const size_t ePos = sci.find('e');
    std::string digits;
    for (const char c : sci.substr(0, ePos)) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    int exponent = 0;
    {
        std::string_view expText = sci.substr(ePos + 1);
        if (!expText.empty() && expText.front() == '+') {
            expText.remove_prefix(1);
        }
        std::from_chars(expText.data(), expText.data() + expText.size(), exponent);
    }
    return layoutFloatText(digits, exponent, negative);
}

/// The value to hand to a text formatter: a NaN becomes the standard quiet NaN, which has no
/// sign bit set. A NaN's sign says nothing about a value, and text never shows it (`floatText`;
/// the language reference, 2.5.2: "a NaN has no sign"). Every path that formats a number through
/// a library (the interpreter hands floats to fmt) applies this first, so no path can write
/// `-nan`, with or without a format specification.
template <std::floating_point T> T withoutNaNSign(T value) {
    return std::isnan(value) ? std::numeric_limits<T>::quiet_NaN() : value;
}

/// The text of one part of a complex number: a whole part is written as an integer (`3`, so
/// that `3+4i` is the text of the value, as design principles 5.1 writes it), every other part
/// by floatText. `-0` is written `0` as a part: the sign of a zero part is not shown.
template <std::floating_point T> std::string complexPartText(T value) {
    if (std::isfinite(value) && value == std::trunc(value) && std::abs(value) < T(1e16)) {
        char buffer[32];
        const auto [end, ec] =
            std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
        if (ec == std::errc()) {
            return std::string(buffer, static_cast<size_t>(end - buffer));
        }
    }
    if (value == 0) {
        return "0";
    }
    return floatText(value);
}

/// The text of a complex number: the real part, the sign of the imaginary one, its magnitude
/// and `i` (`3+4i`, `3-4i`, `0+1i`). Both parts are always written, so a real-looking complex
/// value still reads as a complex one.
template <std::floating_point T> std::string complexText(T real, T imaginary) {
    std::string text    = complexPartText(real);
    const bool negative = !std::isnan(imaginary) && std::signbit(imaginary);
    text += negative ? '-' : '+';
    text += complexPartText(negative ? -imaginary : imaginary);
    text += 'i';
    return text;
}

/// The two parts of the text of a complex number, each still to be read as a number. `real` is
/// empty when the text holds only an imaginary part (`-2i`, `4.5i`).
struct ComplexTextParts {
    std::string_view real;
    std::string_view imaginary;
};

/// Splits the text of a complex number as the language writes one — the real part, the sign of
/// the imaginary part and the imaginary part (`3+4i`, `3-4i`, `0.5-0.25i`), or only the
/// imaginary part with its sign (`4i`, `-2i`) — into the two part texts. Nothing is read here:
/// the caller reads each part by the rule of the number type, so a complex value and a real one
/// are read by the same rule. Returns false when `text` is not written as a complex number: it
/// must end in `i`, and its imaginary part must not be empty (`1+` is not one). A sign that
/// belongs to an exponent (`1e-3i`, `-1e-3i`) is not a separator. A text without `i` is not a
/// complex text at all; a caller that accepts a real number as the real part (entry arguments:
/// a literal takes its type from context, design principles 5.1) reads it as a number when this
/// returns false.
inline bool splitComplexText(std::string_view text, ComplexTextParts &out) {
    if (text.size() < 2 || text.back() != 'i') {
        return false;
    }
    text.remove_suffix(1);
    if (text.empty()) {
        return false;
    }
    // The imaginary part begins at its sign: the last '+' or '-' that is not the number's own
    // leading sign and not the sign of an exponent. Without one the text is only the imaginary
    // part (`2i`, `-2i`).
    std::size_t at = std::string_view::npos;
    for (std::size_t i = text.size(); i-- > 1;) {
        const char c = text[i];
        if ((c == '+' || c == '-') && text[i - 1] != 'e' && text[i - 1] != 'E') {
            at = i;
            break;
        }
    }
    if (at == std::string_view::npos) {
        out.real      = {};
        out.imaginary = text;
        return true;
    }
    if (at + 1 >= text.size()) {
        return false;
    }
    out.real      = text.substr(0, at);
    out.imaginary = text.substr(at);
    return true;
}

/// Whether `value`, truncated toward zero, is a value of the signed integer type T. False for
/// a NaN and for the infinities.
template <std::signed_integral T, std::floating_point F> bool truncatesInto(F value) {
    if (!std::isfinite(value)) {
        return false;
    }
    // Both bounds are powers of two, exact in every floating-point type: -(2^(N-1)) is the
    // smallest value that fits, 2^(N-1) the smallest that does not.
    const F truncated = std::trunc(value);
    const F lower     = static_cast<F>(std::numeric_limits<T>::min());
    return truncated >= lower && truncated < -lower;
}

} // namespace camel::utils
