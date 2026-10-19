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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operator_rules.h"

#include "fault_sites.h"

#include <format>
#include <string>
#include <unordered_map>

namespace camel::passes::cpp {

using camel::core::error::RuntimeDiag;
using camel::core::type::TypeCode;

std::string OperatorCall::operand(size_t i) const {
    const CppValue &v = norm(i);
    if (!v.literal || normType(i) == nullptr || normType(i)->code() != TypeCode::Int64) {
        return v.text;
    }
    for (size_t k = 0; k < normCount(); ++k) {
        if (!norm(k).literal) {
            return v.text; // the other operand fixes the overload
        }
    }
    return std::format("int64_t{{{}}}", v.text);
}

namespace {

using Rules = std::unordered_map<std::string, OperatorRule, std::hash<std::string>>;

/// The marks of the first `n` values a runtime function passes to fault().
std::vector<std::string> marks(size_t n) {
    std::vector<std::string> out;
    for (size_t k = 0; k < n; ++k) {
        out.push_back(SiteTable::valueMark(k));
    }
    return out;
}

std::string joinedNorm(OperatorCall &call, size_t from = 0) {
    std::string out;
    for (size_t i = from; i < call.normCount(); ++i) {
        if (!out.empty()) {
            out += ", ";
        }
        out += call.norm(i).text;
    }
    return out;
}

std::string joinedWith(OperatorCall &call, size_t from = 0) {
    std::string out;
    for (size_t i = from; i < call.withCount(); ++i) {
        if (!out.empty()) {
            out += ", ";
        }
        out += call.with(i).text;
    }
    return out;
}

OperatorRule infix(const char *symbol) {
    return {
        [symbol](OperatorCall &c) -> std::optional<std::string> {
            return std::format("({} {} {})", c.norm(0).text, symbol, c.norm(1).text);
        },
        true,
    };
}

OperatorRule prefix(const char *symbol) {
    return {
        [symbol](OperatorCall &c) -> std::optional<std::string> {
            // Parenthesized, and the operand too when it starts with an operator itself
            // (`-(-x)`, never `--x`).
            const std::string &x = c.norm(0).text;
            const bool bare      = !x.empty() && x.find_first_of("+-!~(") != 0;
            return bare ? std::format("({}{})", symbol, x) : std::format("({}({}))", symbol, x);
        },
        true,
    };
}

/// An operator of the standalone runtime that cannot fault: `rt::name(a, b, ...)`.
OperatorRule runtimeFunction(const char *name, bool inlinable = true) {
    return {
        [name](OperatorCall &c) -> std::optional<std::string> {
            std::string args;
            for (size_t i = 0; i < c.normCount(); ++i) {
                args += (i ? ", " : "") + c.operand(i);
            }
            return std::format("rt::{}({})", name, args);
        },
        inlinable,
    };
}

/// One that can: `rt::name(a, b, ..., site)`, the site's report taking `values` values.
OperatorRule faultingFunction(const char *name, RuntimeDiag kind, size_t values) {
    return {
        [name, kind, values](OperatorCall &c) -> std::optional<std::string> {
            std::string args;
            for (size_t i = 0; i < c.normCount(); ++i) {
                args += c.operand(i) + ", ";
            }
            return std::format("rt::{}({}{})", name, args, c.site(kind, marks(values)));
        },
        false,
    };
}

/// `rt::name(x, w)`: the value and the one `with` argument (`xs->append<x>`).
OperatorRule withArgument(const char *name) {
    return {
        [name](OperatorCall &c) -> std::optional<std::string> {
            return std::format("rt::{}({}, {})", name, c.norm(0).text, c.with(0).text);
        },
        true,
    };
}

/// `x.name()`: a part of a tagged value (opt.h).
OperatorRule method(const char *name) {
    return {
        [name](OperatorCall &c) -> std::optional<std::string> {
            return std::format("{}.{}()", c.norm(0).text, name);
        },
        true,
    };
}

OperatorRule cast(const char *type) {
    return {
        [type](OperatorCall &c) -> std::optional<std::string> {
            return std::format("static_cast<{}>({})", type, c.norm(0).text);
        },
        true,
    };
}

OperatorRule identity() {
    return {[](OperatorCall &c) -> std::optional<std::string> { return c.norm(0).text; }, true};
}

/// A conversion that can fault (cast.cpp, camel/core/numeric_convert.h): `rt::fn<To>(x,
/// "target", site)`; the report has three values (numeric_convert.h names them).
OperatorRule
checkedConversion(const char *function, const char *to, const char *target, RuntimeDiag kind) {
    return {
        [function, to, target, kind](OperatorCall &c) -> std::optional<std::string> {
            return std::format(
                "rt::{}<{}>({}, \"{}\", {})",
                function,
                to,
                c.norm(0).text,
                target,
                c.site(kind, marks(3)));
        },
        false,
    };
}

/// `std::f(x)` of <cmath> (modules/math/operators.cpp). With `toFloat`, an integer argument
/// is converted first, as __math_sqrt__ does: an i32 to f32, an int to float.
OperatorRule cmath(const char *function, bool toFloat = false) {
    return {
        [function, toFloat](OperatorCall &c) -> std::optional<std::string> {
            const Type *t = c.normType(0);
            std::string x = c.norm(0).text;
            if (toFloat && t != nullptr && t->code() == TypeCode::Int32) {
                x = std::format("static_cast<float>({})", x);
            } else if (toFloat && t != nullptr && t->code() == TypeCode::Int64) {
                x = std::format("static_cast<double>({})", x);
            } else if (c.norm(0).literal && t != nullptr && t->code() == TypeCode::Int64) {
                x = std::format("int64_t{{{}}}", x);
            }
            return std::format("std::{}({})", function, x);
        },
        true,
    };
}

/// `println` and `print` (src/builtin/operators/io.cpp): without a format string the texts
/// separated by blanks; with one (the `with` argument) the formatted text.
OperatorRule printing(const char *plain, const char *formatted) {
    return {
        [plain, formatted](OperatorCall &c) -> std::optional<std::string> {
            if (c.withCount() == 0) {
                return std::format("rt::{}({})", plain, joinedNorm(c));
            }
            const std::string values = joinedNorm(c);
            return std::format(
                "rt::{}({}, {}{}{})",
                formatted,
                c.with(0).text,
                c.site(RuntimeDiag::FormatFailed, marks(2)),
                values.empty() ? "" : ", ",
                values);
        },
        false,
    };
}

Rules buildRules() {
    Rules r;

    // ---- Arithmetic (ops.cpp: __builtin__add_ll__ ... __builtin__pow__) ----
    for (const char *t : {"i", "l"}) {
        r[std::format(":op/add_{}", t)] = runtimeFunction("add");
        r[std::format(":op/sub_{}", t)] = runtimeFunction("sub");
        r[std::format(":op/mul_{}", t)] = runtimeFunction("mul");
        r[std::format(":op/neg_{}", t)] = runtimeFunction("neg");
        r[std::format(":op/mod_{}", t)] = faultingFunction("mod", RuntimeDiag::DivisionByZero, 0);
        r[std::format(":op/inv_{}", t)] = prefix("~");
    }
    for (const char *t : {"f", "d"}) {
        r[std::format(":op/add_{}", t)] = infix("+");
        r[std::format(":op/sub_{}", t)] = infix("-");
        r[std::format(":op/mul_{}", t)] = infix("*");
        r[std::format(":op/neg_{}", t)] = prefix("-");
    }
    for (const char *t : {"i", "l", "f", "d"}) {
        r[std::format(":op/div_{}", t)] = faultingFunction("div", RuntimeDiag::DivisionByZero, 0);
        r[std::format(":op/pow_{}", t)] = runtimeFunction("pow");
    }

    // ---- Comparisons (ops.cpp: __builtin__eq__, __builtin__lt_s__, ...) ----
    // Floats compare as numbers (NaN equals nothing); strings by their bytes.
    for (const char *t : {"i", "l", "f", "d", "s"}) {
        r[std::format(":op/lt_{}", t)] = infix("<");
        r[std::format(":op/le_{}", t)] = infix("<=");
        r[std::format(":op/gt_{}", t)] = infix(">");
        r[std::format(":op/ge_{}", t)] = infix(">=");
    }
    for (const char *t : {"i", "l", "f", "d", "b", "s"}) {
        r[std::format(":op/eq_{}", t)] = infix("==");
        r[std::format(":op/ne_{}", t)] = infix("!=");
    }
    r[":op/eq"] = infix("==");
    r[":op/ne"] = infix("!=");

    // ---- Booleans: `&` and `|` evaluate both sides (design principles 2.11); here both
    // operands are values already, so && and || compute the same.
    r[":op/not"] = prefix("!");
    r[":op/and"] = infix("&&");
    r[":op/or"]  = infix("||");

    // ---- Numbers between types (cast.cpp) ----
    r[":op/itoi"] = identity();
    r[":op/ltol"] = identity();
    r[":op/ftof"] = identity();
    r[":op/dtod"] = identity();
    r[":op/stos"] = identity();
    r[":op/itol"] = cast("int64_t");
    r[":op/itof"] = cast("float");
    r[":op/ltof"] = cast("float");
    r[":op/dtof"] = cast("float");
    r[":op/itod"] = cast("double");
    r[":op/ltod"] = cast("double");
    r[":op/ftod"] = cast("double");
    r[":op/ltoi"] =
        checkedConversion("narrow", "int32_t", "i32", RuntimeDiag::ConversionOutOfRange);
    r[":op/ftoi"] =
        checkedConversion("truncate", "int32_t", "i32", RuntimeDiag::ConversionOutOfRange);
    r[":op/dtoi"] =
        checkedConversion("truncate", "int32_t", "i32", RuntimeDiag::ConversionOutOfRange);
    r[":op/ftol"] =
        checkedConversion("truncate", "int64_t", "int", RuntimeDiag::ConversionOutOfRange);
    r[":op/dtol"] =
        checkedConversion("truncate", "int64_t", "int", RuntimeDiag::ConversionOutOfRange);
    r[":op/stoi"] =
        checkedConversion("integerFromText", "int32_t", "i32", RuntimeDiag::InvalidNumberText);
    r[":op/stol"] =
        checkedConversion("integerFromText", "int64_t", "int", RuntimeDiag::InvalidNumberText);
    r[":op/stof"] =
        checkedConversion("floatFromText", "float", "f32", RuntimeDiag::InvalidNumberText);
    r[":op/stod"] =
        checkedConversion("floatFromText", "double", "float", RuntimeDiag::InvalidNumberText);
    for (const char *t : {"i", "l", "f", "d", "a"}) {
        r[std::format(":op/{}tos", t)] = runtimeFunction("toText");
    }

    // ---- Strings (ops.cpp __builtin__add_ss__, __builtin__idx__; struct.cpp) ----
    r[":op/add_s"]            = infix("+");
    r[":struct/len_str"]      = runtimeFunction("length");
    r[":op/idx_str"]          = faultingFunction("charAt", RuntimeDiag::IndexOutOfRange, 3);
    r[":str/join"]            = withArgument("join");
    r[":struct/contains_str"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format("rt::contains({}, {})", c.norm(0).text, c.with(0).text);
        },
        true,
    };

    // ---- Output and text (io.cpp, str.cpp) ----
    r[":io/println"] = printing("println", "printlnFormatted");
    r[":io/print"]   = printing("print", "printFormatted");
    r[":str/format"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const std::string values = joinedWith(c);
            return std::format(
                "rt::format({}, {}{}{})",
                c.norm(0).text,
                c.site(RuntimeDiag::FormatFailed, marks(2)),
                values.empty() ? "" : ", ",
                values);
        },
        false,
    };

    // ---- Graphs (operators/graph.h) ----
    // A function value of the generated program is its own C++ function: there is no copy of
    // a graph to take back to its original.
    r[":graph/original"] = {
        [](OperatorCall &c) -> std::optional<std::string> { return c.norm(0).text; },
        true,
    };

    // ---- Arrays (struct.cpp, ops.cpp __builtin__idx__) ----
    r[":op/idx_arr"]          = faultingFunction("at", RuntimeDiag::IndexOutOfRange, 3);
    r[":struct/len_arr"]      = runtimeFunction("length");
    r[":struct/head_arr"]     = faultingFunction("head", RuntimeDiag::IndexOutOfRange, 3);
    r[":struct/tail_arr"]     = runtimeFunction("tail");
    r[":struct/concat_arr"]   = runtimeFunction("concat");
    r[":struct/append_arr"]   = withArgument("append");
    r[":struct/extend_arr"]   = withArgument("concat");
    r[":struct/contains_arr"] = withArgument("contains");
    r[":struct/zip"]          = {
        [](OperatorCall &c) -> std::optional<std::string> {
            std::string args;
            for (size_t i = 0; i < c.normCount(); ++i) {
                args += ", " + c.norm(i).text;
            }
            return std::format(
                "rt::zip<{}>({}{})",
                c.cppType(c.resultType()),
                c.site(RuntimeDiag::InvalidArgument, marks(2)),
                args);
        },
        false,
    };
    r[":struct/slice_arr"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format(
                "rt::slice({}, {}, {}, {})",
                c.norm(0).text,
                c.with(0).text,
                c.with(1).text,
                c.site(RuntimeDiag::SliceOutOfRange, marks(4)));
        },
        false,
    };
    r[":struct/range"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format(
                "rt::range({}, {}, {}, {})",
                c.norm(0).text,
                c.norm(1).text,
                c.normCount() > 2 ? c.norm(2).text : std::string("1"),
                c.site(RuntimeDiag::InvalidArgument, marks(2)));
        },
        false,
    };

    // ---- T?, T! and the error value (tagged.cpp) ----
    r[":op/tag_is_null"]     = method("isNull");
    r[":op/tag_null_value"]  = method("value");
    r[":op/tag_is_error"]    = method("failed");
    r[":op/tag_error_value"] = method("value");
    r[":op/tag_error_info"]  = method("error");
    r[":op/tag_throw"]       = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format("{}::failure({})", c.cppType(c.resultType()), c.norm(0).text);
        },
        true,
    };
    r[":op/error_new"]   = runtimeFunction("makeError");
    r[":op/parse_int"]   = runtimeFunction("parseInt", false);
    r[":op/parse_float"] = runtimeFunction("parseFloat", false);

    // ---- Modules with a standalone translation ----
    r["time:now"]         = runtimeFunction("now", false);
    r["sys:get_env"]      = runtimeFunction("getEnv", false);
    r["statistics:mean"]  = runtimeFunction("statisticsMean");
    r["statistics:stdev"] = runtimeFunction("statisticsStdev");
    // random draws as modules/random does, through camel/utils/random.h (standalone/random.h).
    r["rand:seed"]    = runtimeFunction("randomSeed", false);
    r["rand:rand"]    = runtimeFunction("randomUnit", false);
    r["rand:randn"]   = runtimeFunction("randomNormal", false);
    r["rand:randint"] = runtimeFunction("randomBetween", false);
    r["rand:choice"]  = faultingFunction("randomChoice", RuntimeDiag::InvalidArgument, 2);
    r["rand:sample"]  = faultingFunction("randomSample", RuntimeDiag::InvalidArgument, 2);
    r["rand:shuffle"] = runtimeFunction("randomShuffle", false);
    r["math:sqrt"]    = cmath("sqrt", true);
    for (const char *t : {"i", "l"}) {
        r[std::format("math:abs_{}", t)] = cmath("abs");
    }
    for (const char *t : {"f", "d"}) {
        r[std::format("math:abs_{}", t)]   = cmath("fabs");
        r[std::format("math:exp_{}", t)]   = cmath("exp");
        r[std::format("math:round_{}", t)] = cmath("round");
        r[std::format("math:ceil_{}", t)]  = cmath("ceil");
        r[std::format("math:floor_{}", t)] = cmath("floor");
    }

    // ---- The end of the program (`exit(n)`, `os.exit(n)`) ----
    for (const char *uri : {":exit", "exit", "os:exit"}) {
        r[uri] = {
            [](OperatorCall &c) -> std::optional<std::string> {
                return std::format("rt::exit({})", c.norm(0).text);
            },
            false,
        };
    }
    return r;
}

} // namespace

const OperatorRule *findOperatorRule(std::string_view uri) {
    static const Rules rules = buildRules();
    auto it                  = rules.find(std::string(uri));
    return it == rules.end() ? nullptr : &it->second;
}

} // namespace camel::passes::cpp
