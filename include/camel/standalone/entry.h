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
 * them in one report. The reports are those of `camel`, byte for byte (EntryArgumentInvalid,
 * EntryArgumentMissing at the entry function): the compiler renders them into two Sites with the
 * values left as marks (core.h), and they end the program as a fault does (exit code 1, the
 * report on standard output). The translated types are the scalars, the complex numbers
 * included; a parameter of another type is refused when the program is translated.
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

/// How the value of a parameter of `main` is read from its text.
enum class EntryKind {
    Int,   // an integer: an optional `+`, then the digits of an int64 (no range check for i32)
    Float, // one number token, read by camel::utils::parseWholeFloat
    C64,   // a c64: the text of a complex number, its parts read as floats of that width
    C128,  // a c128: the same text, its parts read as doubles
    Bool,  // `true` or `false`
    Text,  // the text as it is
};

/// A parameter of `main`: its name and how its value is read.
struct EntryParameter {
    const char *name;
    EntryKind kind;
};

/// The values the command line gives the parameters of `main` (see the file comment).
class EntryArguments {
  public:
    /// Reads `--arg name=value` (also `--arg=name=value`) from the command line. `invalid` and
    /// `missing` are the reports of EntryArgumentInvalid (values: the argument, why it is
    /// wrong) and EntryArgumentMissing (values: "parameter" or "parameters", their names).
    EntryArguments(
        int argc, char **argv, std::initializer_list<EntryParameter> parameters,
        const Site &invalid, const Site &missing)
        : parameters_(parameters), values_(parameters.size()), given_(parameters.size(), false) {
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
    double number(std::size_t index) const { return values_[index].number; }
    std::complex<float> c64(std::size_t index) const { return values_[index].complexF; }
    std::complex<double> c128(std::size_t index) const { return values_[index].complexD; }
    bool boolean(std::size_t index) const { return values_[index].boolean; }
    Str text(std::size_t index) const { return Str(values_[index].text); }

  private:
    struct Value {
        int64_t integer               = 0;
        double number                 = 0;
        std::complex<float> complexF  = {};
        std::complex<double> complexD = {};
        bool boolean                  = false;
        std::string text;
    };

    /// One number as `camel` reads one: the language's whole-token float parser (camel/utils/
    /// number_text.h), not strtod's wider grammar. No hexadecimal form, no `Infinity`, no
    /// blanks; an optional sign, digits with an optional fraction and exponent, `inf` or `nan`.
    template <std::floating_point T> static bool readNumber(std::string_view text, T &out) {
        return camel::utils::parseWholeFloat<T>(text, out) ==
               camel::utils::NumberTextStatus::Ok;
    }

    /// A complex number as `camel` reads one: the two parts are numbers (camel/utils/
    /// number_text.h splits the text; the parts are read by readNumber at the declared width).
    /// A text that is not written as a complex number is read as a real one, whose imaginary
    /// part is zero: a literal takes its type from context (design principles 5.1), so `1.5` is
    /// `1.5+0i`.
    template <std::floating_point T>
    static bool readComplex(const std::string &text, T &real, T &imaginary) {
        camel::utils::ComplexTextParts parts;
        imaginary = T{};
        if (!camel::utils::splitComplexText(text, parts)) {
            real = T{};
            return readNumber(text, real);
        }
        real = T{};
        return (parts.real.empty() || readNumber(parts.real, real)) &&
               readNumber(parts.imaginary, imaginary);
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
            const char *first = text.data();
            const char *last  = text.data() + text.size();
            if (first != last && *first == '+') {
                ++first;
            }
            const auto result = std::from_chars(first, last, value.integer);
            if (result.ec != std::errc() || result.ptr != last || first == last) {
                fault(invalid, {name, "'" + text + "' is not an integer"});
            }
            break;
        }
        case EntryKind::Float: {
            if (!readNumber(text, value.number)) {
                fault(invalid, {name, "'" + text + "' is not a number"});
            }
            break;
        }
        case EntryKind::C64:
        case EntryKind::C128: {
            bool readable = false;
            if (parameters_[index].kind == EntryKind::C64) {
                float real      = 0.0f;
                float imaginary = 0.0f;
                readable         = readComplex(text, real, imaginary);
                value.complexF   = {real, imaginary};
            } else {
                double real      = 0.0;
                double imaginary = 0.0;
                readable         = readComplex(text, real, imaginary);
                value.complexD   = {real, imaginary};
            }
            if (!readable) {
                fault(
                    invalid,
                    {name,
                     "'" + text +
                         "' is not a complex number (write 1+2i, -2i, 0.5-0.25i or a real "
                         "number such as 1.5)"});
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
    std::vector<Value> values_;
    std::vector<bool> given_;
};

} // namespace camel::standalone
