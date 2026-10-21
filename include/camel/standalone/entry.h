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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The parameters of `main` in a program std::cpp writes. They are the inputs of the program and
 * are given on its command line as `camel` takes them (design principles 9.8, 10.6):
 *
 *     ./program --arg n=3 --arg name=hi
 *
 * Each value is read by the parameter's declared type exactly as src/execute/entry_args.cpp
 * reads it (an integer, a number token, a complex number in the language's text (`1+2i`, `-2i`;
 * a real number such as `1.5` is its real part with an imaginary part of zero), `true`/`false`,
 * the text as it is), the arguments are checked in the order they are written
 * and the first that is wrong is reported, then a parameter given no value is reported, all of
 * them in one report. A number text is read at the width the parameter is declared with (an
 * `i32` is read as an int32 and an `f32` as a float), so a value the declared type cannot hold
 * is refused with that type's name and its range. A number text that cannot be read is refused
 * with the sentence of its cause, rendered from the catalogue (InvalidNumberText when it is not
 * a number, ConversionOutOfRange when the type cannot hold it, with the range). The reports are
 * those of `camel`, byte for byte (EntryArgumentInvalid, EntryArgumentMissing at the entry
 * function): the compiler renders them into Sites with the values left as marks (core.h), and
 * they end the program as a fault does (exit code 1, the report on standard output). The
 * translated types are the scalars, the complex numbers included; a parameter of another type is
 * refused when the program is translated.
 */

#pragma once

#include "camel/standalone/core.h"
#include "camel/standalone/str.h"
#include "camel/utils/number_text.h"

#include <charconv>
#include <complex>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace camel::standalone {

/// How the value of a parameter of `main` is read from its text. Each integer and float kind
/// reads the text at the width the parameter is declared with: an `i32` is an int32 all the way
/// (a text outside its range is refused with the range of `i32`, not read as an int64 and
/// narrowed), and an `f32` is a float all the way (design principles 5.1).
enum class EntryKind {
    Int,     // an `int`: an optional `+`, then the digits of an int64
    Int32,   // an `i32`: the same grammar, read as an int32
    Float,   // a `float`: one number token, read by camel::utils::parseWholeFloat<double>
    Float32, // an `f32`: the same grammar, read as a float
    C64,     // a c64: the text of a complex number, its parts read as floats of that width
    C128,    // a c128: the same text, its parts read as doubles
    Bool,    // `true` or `false`
    Text,    // the text as it is
};

/// A parameter of `main`: its name and how its value is read.
struct EntryParameter {
    const char *name;
    EntryKind kind;
};

/// The sentences a number parameter's text is refused with, rendered by the compiler from the
/// diagnostics catalogue with the text left as mark 0 (core.h): InvalidNumberText when the text
/// is not a number, ConversionOutOfRange when it is a number the parameter's type cannot hold.
/// A complex parameter has a second ConversionOutOfRange sentence for its imaginary part (the
/// real part uses `outOfRange`); the other kinds leave these empty.
struct EntryNumberText {
    const char *notANumber          = nullptr;
    const char *outOfRange          = nullptr;
    const char *outOfRangeImaginary = nullptr;
    /// A number written as an integer token whose value does not fit an int64: the sentence
    /// names the integer's range, not the float's, which would be false about the text.
    const char *outOfRangeInteger = nullptr;
};

/// The values the command line gives the parameters of `main` (see the file comment).
class EntryArguments {
  public:
    /// Reads `--arg name=value` (also `--arg=name=value`) from the command line. `numberText`
    /// gives, for each parameter in order, the sentences its number text is refused with (empty
    /// for a parameter that is not a number; see EntryNumberText). `invalid` and `missing` are
    /// the reports of EntryArgumentInvalid (values: the argument, why it is wrong) and
    /// EntryArgumentMissing (values: "parameter" or "parameters", their names).
    EntryArguments(
        int argc, char **argv, std::initializer_list<EntryParameter> parameters,
        std::initializer_list<EntryNumberText> numberText, const Site &invalid,
        const Site &missing)
        : parameters_(parameters), numberText_(numberText), values_(parameters.size()),
          given_(parameters.size(), false) {
        std::vector<std::string> specs;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--arg" && i + 1 < argc) {
                specs.emplace_back(argv[++i]);
            } else if (arg.starts_with("--arg=")) {
                specs.emplace_back(arg.substr(6));
            } else {
                // Anything else is not an argument of `main` written as it must be.
                fault(invalid, {std::string(arg), "an argument is written name=value"});
            }
        }
        // Every argument is well formed before any is bound, as camel parses them first.
        for (const std::string &spec : specs) {
            const std::size_t eq = spec.find('=');
            if (eq == std::string::npos || eq == 0) {
                fault(invalid, {spec, "an argument is written name=value"});
            }
        }
        for (const std::string &spec : specs) {
            bind(spec.substr(0, spec.find('=')), spec.substr(spec.find('=') + 1), invalid);
        }
        std::vector<std::string> open;
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            if (!given_[i]) {
                open.emplace_back(parameters_[i].name);
            }
        }
        if (!open.empty()) {
            std::string names;
            for (std::size_t i = 0; i < open.size(); ++i) {
                names += (i == 0 ? "'" : i + 1 == open.size() ? " and '" : ", '") + open[i] + "'";
            }
            fault(missing, {open.size() == 1 ? "parameter" : "parameters", names});
        }
    }

    /// The value of the parameter at `index`, read by its kind (the one it was declared with).
    int64_t integer(std::size_t index) const { return values_[index].integer; }
    int32_t integer32(std::size_t index) const { return values_[index].integer32; }
    double number(std::size_t index) const { return values_[index].number; }
    float number32(std::size_t index) const { return values_[index].number32; }
    std::complex<float> c64(std::size_t index) const { return values_[index].complexF; }
    std::complex<double> c128(std::size_t index) const { return values_[index].complexD; }
    bool boolean(std::size_t index) const { return values_[index].boolean; }
    Str text(std::size_t index) const { return Str(values_[index].text); }

  private:
    struct Value {
        int64_t integer               = 0;
        int32_t integer32             = 0;
        double number                 = 0;
        float number32                = 0.0f;
        std::complex<float> complexF  = {};
        std::complex<double> complexD = {};
        bool boolean                  = false;
        std::string text;
    };

    /// What reading a number text gave: the value, or which refusal it is. A complex text has
    /// one more cause than a real one: its imaginary part.
    enum class ReadStatus { Ok, NotANumber, OutOfRange, OutOfRangeImaginary, OutOfRangeInteger };

    /// One integer as `camel` reads one: the language's whole-token integer parser, at the width
    /// the parameter is declared with (`i32` reads an int32, so its range is the range checked).
    template <std::signed_integral T> static ReadStatus readInteger(std::string_view text, T &out) {
        switch (camel::utils::parseNumberToken<T>(text, out)) {
        case camel::utils::NumberTextStatus::Ok:
            return ReadStatus::Ok;
        case camel::utils::NumberTextStatus::OutOfRange:
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            return ReadStatus::OutOfRange;
        default:
            return ReadStatus::NotANumber;
        }
    }

    /// One number as `camel` reads one: one whole number token of the language (camel/utils/
    /// number_text.h), not strtod's wider grammar: an optional sign, an integer token (`0xFF`,
    /// `0b1010`, `0o77`; a leading zero is decimal data text, H15i) or a real token (digits with
    /// an optional fraction and exponent), `inf` or `nan`. A nonzero real token too small to
    /// tell from zero is refused as out of range, like a source literal (H15i). A number the
    /// width cannot hold is OutOfRange, not NotANumber; an integer token whose value does not
    /// fit an int64 is OutOfRangeInteger.
    template <std::floating_point T> static ReadStatus readNumber(std::string_view text, T &out) {
        switch (camel::utils::parseNumberToken<T>(text, out)) {
        case camel::utils::NumberTextStatus::Ok:
            return ReadStatus::Ok;
        case camel::utils::NumberTextStatus::Underflow:
        case camel::utils::NumberTextStatus::OutOfRange:
            return ReadStatus::OutOfRange;
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            return ReadStatus::OutOfRangeInteger;
        default:
            return ReadStatus::NotANumber;
        }
    }

    /// A complex number as `camel` reads one: the two parts are numbers (camel/utils/
    /// number_text.h splits the text; the parts are read by readNumber at the declared width).
    /// A text that is not written as a complex number is read as a real one, whose imaginary
    /// part is zero: a literal takes its type from context (design principles 5.1), so `1.5` is
    /// `1.5+0i`.
    template <std::floating_point T>
    static ReadStatus readComplex(const std::string &text, T &real, T &imaginary) {
        camel::utils::ComplexTextParts parts;
        real      = T{};
        imaginary = T{};
        if (!camel::utils::splitComplexText(text, parts)) {
            return readNumber(text, real);
        }
        if (!parts.real.empty()) {
            const ReadStatus status = readNumber(parts.real, real);
            if (status != ReadStatus::Ok) {
                return status;
            }
        }
        const ReadStatus status = readNumber(parts.imaginary, imaginary);
        return status == ReadStatus::OutOfRange ? ReadStatus::OutOfRangeImaginary : status;
    }

    /// The reason clause for a number text refused with `status`: the catalogued sentence the
    /// compiler rendered into the parameter's EntryNumberText, with the text in place of the mark
    /// and its final period dropped (EntryArgumentInvalid adds one).
    std::string refusal(std::size_t index, ReadStatus status, const std::string &text) const {
        static const EntryNumberText none;
        const EntryNumberText &texts = index < numberText_.size() ? numberText_[index] : none;
        const char *message          = nullptr;
        switch (status) {
        case ReadStatus::NotANumber:
            message = texts.notANumber;
            break;
        case ReadStatus::OutOfRange:
            message = texts.outOfRange;
            break;
        case ReadStatus::OutOfRangeImaginary:
            message = texts.outOfRangeImaginary;
            break;
        case ReadStatus::OutOfRangeInteger:
            message = texts.outOfRangeInteger;
            break;
        case ReadStatus::Ok:
            break;
        }
        std::string reason = withValues(message != nullptr ? message : "", {text});
        if (!reason.empty() && reason.back() == '.') {
            reason.pop_back();
        }
        return reason;
    }

    void bind(const std::string &name, const std::string &text, const Site &invalid) {
        std::size_t index = parameters_.size();
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            if (name == parameters_[i].name) {
                index = i;
                break;
            }
        }
        if (index == parameters_.size()) {
            std::string known;
            for (const EntryParameter &p : parameters_) {
                known += (known.empty() ? "" : ", ") + std::string(p.name);
            }
            fault(
                invalid,
                {name,
                 parameters_.empty()
                     ? std::string("'main' takes no parameters")
                     : "'main' has no parameter of that name (its parameters: " + known + ")"});
        }
        if (given_[index]) {
            fault(invalid, {name, "the argument is given more than once"});
        }
        given_[index] = true;
        Value &value  = values_[index];
        switch (parameters_[index].kind) {
        case EntryKind::Int: {
            const ReadStatus status = readInteger(text, value.integer);
            if (status != ReadStatus::Ok) {
                fault(invalid, {name, refusal(index, status, text)});
            }
            break;
        }
        case EntryKind::Int32: {
            const ReadStatus status = readInteger(text, value.integer32);
            if (status != ReadStatus::Ok) {
                fault(invalid, {name, refusal(index, status, text)});
            }
            break;
        }
        case EntryKind::Float: {
            const ReadStatus status = readNumber(text, value.number);
            if (status != ReadStatus::Ok) {
                fault(invalid, {name, refusal(index, status, text)});
            }
            break;
        }
        case EntryKind::Float32: {
            const ReadStatus status = readNumber(text, value.number32);
            if (status != ReadStatus::Ok) {
                fault(invalid, {name, refusal(index, status, text)});
            }
            break;
        }
        case EntryKind::C64:
        case EntryKind::C128: {
            ReadStatus status = ReadStatus::Ok;
            if (parameters_[index].kind == EntryKind::C64) {
                float real      = 0.0f;
                float imaginary = 0.0f;
                status          = readComplex(text, real, imaginary);
                value.complexF  = {real, imaginary};
            } else {
                double real      = 0.0;
                double imaginary = 0.0;
                status           = readComplex(text, real, imaginary);
                value.complexD   = {real, imaginary};
            }
            if (status != ReadStatus::Ok) {
                fault(invalid, {name, refusal(index, status, text)});
            }
            break;
        }
        case EntryKind::Bool:
            if (text != "true" && text != "false") {
                fault(invalid, {name, "'" + text + "' is not a bool (write true or false)"});
            }
            value.boolean = text == "true";
            break;
        case EntryKind::Text:
            value.text = text;
            break;
        }
    }

    std::vector<EntryParameter> parameters_;
    std::vector<EntryNumberText> numberText_;
    std::vector<Value> values_;
    std::vector<bool> given_;
};

} // namespace camel::standalone
