// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operator_rules.h"

#include "camel/core/numeric_convert.h"
#include "camel/core/type/composite/array.h"
#include "fault_sites.h"

#include <format>
#include <string>
#include <unordered_map>

namespace camel::passes::cpp {

using camel::core::error::RuntimeDiag;
using camel::core::type::asArrayType;
using camel::core::type::ArrayType;
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

/// A number as a complex value of the width `to` (`c64(x)`, `c128(x)`):
/// `rt::complexFromReal<float>(x)` (include/camel/standalone/complex.h).
OperatorRule complexFromReal(const char *to) {
    return {
        [to](OperatorCall &c) -> std::optional<std::string> {
            return std::format("rt::complexFromReal<{}>({})", to, c.norm(0).text);
        },
        true,
    };
}

/// A complex value in the width `to` (`c64(z)`, `c128(z)`): `rt::complexFromComplex<double>(z)`.
OperatorRule complexFromComplex(const char *to) {
    return {
        [to](OperatorCall &c) -> std::optional<std::string> {
            return std::format("rt::complexFromComplex<{}>({})", to, c.norm(0).text);
        },
        true,
    };
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

/// A text conversion that can fault with either cause (cast.cpp, camel/core/numeric_convert.h):
/// `rt::fn<To>(x, invalidSite, rangeSite)`. The two reports say what a text that is not a number
/// is refused with, and what a number the type cannot hold is refused with (the target, what the
/// text must be, and the range are in the rendered messages); only the text itself is a value
/// when the fault happens.
OperatorRule checkedTextConversion(
    const char *function, const char *to, const char *target, const char *what,
    std::string rangeReason) {
    return {
        [function, to, target, what, rangeReason = std::move(rangeReason)](
            OperatorCall &c) -> std::optional<std::string> {
            const std::string text = SiteTable::valueMark(0);
            return std::format(
                "rt::{}<{}>({}, {}, {})",
                function,
                to,
                c.norm(0).text,
                c.site(RuntimeDiag::InvalidNumberText, {text, target, what}),
                c.site(
                    RuntimeDiag::ConversionOutOfRange,
                    {"'" + text + "'", target, rangeReason}));
        },
        false};
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

    // ---- Complex numbers (ops.cpp complexBinary, complexPower; include/camel/standalone/
    // complex.h) ----
    // `+`, `-`, `*`, unary `-`, `==` and `!=` are std::complex's, whose arithmetic is the two
    // parts' arithmetic in the value's width; `/` faults on the complex zero; `^` is the
    // runtime's complexPower, an integer exponent by repeated squaring and any other through
    // std::pow.
    for (const char *t : {"c", "z"}) {
        r[std::format(":op/add_{}", t)] = infix("+");
        r[std::format(":op/sub_{}", t)] = infix("-");
        r[std::format(":op/mul_{}", t)] = infix("*");
        r[std::format(":op/neg_{}", t)] = prefix("-");
        r[std::format(":op/div_{}", t)] =
            faultingFunction("complexDiv", RuntimeDiag::DivisionByZero, 0);
        r[std::format(":op/eq_{}", t)] = infix("==");
        r[std::format(":op/ne_{}", t)] = infix("!=");
        r[std::format(":op/pow_{}", t)] = runtimeFunction("pow");
        for (const char *e : {"i", "l"}) {
            r[std::format(":op/pow_{}_{}", t, e)] = runtimeFunction("pow");
        }
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

    // ---- Booleans: `&` and `|` evaluate both sides (design principles 4.2); here both
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
    r[":op/stoi"] = checkedTextConversion(
        "integerFromText", "int32_t", "i32", "an integer",
        camel::core::integerRangeReason<int32_t>("i32"));
    r[":op/stol"] = checkedTextConversion(
        "integerFromText", "int64_t", "int", "an integer",
        camel::core::integerRangeReason<int64_t>("int"));
    r[":op/stof"] = checkedTextConversion(
        "floatFromText", "float", "f32", "a number", camel::core::floatRangeReason<float>("f32"));
    r[":op/stod"] = checkedTextConversion(
        "floatFromText",
        "double",
        "float",
        "a number",
        camel::core::floatRangeReason<double>("float"));
    for (const char *t : {"i", "l", "f", "d", "a"}) {
        r[std::format(":op/{}tos", t)] = runtimeFunction("toText");
    }
    // A number to a complex value, and a complex value to the other width (cast.cpp, the c64
    // and c128 groups of globals.cpp): the imaginary part of a number is zero.
    r[":op/ctoc"] = identity();
    r[":op/ztoz"] = identity();
    for (const char *from : {"i", "l", "f", "d"}) {
        r[std::format(":op/{}toc", from)] = complexFromReal("float");
        r[std::format(":op/{}toz", from)] = complexFromReal("double");
    }
    r[":op/ztoc"] = complexFromComplex("float");
    r[":op/ctoz"] = complexFromComplex("double");
    r[":op/ctos"] = runtimeFunction("toText");
    r[":op/ztos"] = runtimeFunction("toText");

    // ---- Strings (ops.cpp __builtin__add_ss__, __builtin__idx__; struct.cpp) ----
    r[":op/add_s"]            = infix("+");
    r[":struct/len_str"]      = runtimeFunction("length");
    r[":op/idx_str"]          = faultingFunction("charAt", RuntimeDiag::IndexOutOfRange, 3);
    r[":struct/iter_str"]     = runtimeFunction("chars");
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

    // ---- Arrays (struct.cpp, ops.cpp __builtin__idx__) ----
    // A position is the element itself only when the value is a rank-1 array, which is what
    // `rt::at` gives. An `rt::Array` here is a flat run of elements with no shape
    // (cpp_types.cpp), so a position on a multidimensional value -- the slab this takes for a row
    // of a rank-2 array, design principles 5.4 -- and a position on an open-rank value have no
    // value in this back end: the operator runs through the bridge (camel/standalone/bridge.h).
    // The axis word a call of the index writes is the same answer's: only the first dimension's
    // position, the two-word call, is this rule's.
    r[":op/idx_arr"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            const bool takesASlab =
                value != nullptr && (value->anyRank() || value->rank() >= 2);
            if (c.normCount() != 2 || takesASlab) {
                return std::nullopt;
            }
            return std::format(
                "rt::at({}, {}, {})",
                c.operand(0),
                c.operand(1),
                c.site(RuntimeDiag::IndexOutOfRange, marks(3)));
        },
        false,
    };
    // A store into an element or a field as a value (`xs[i] = v`, `s.x = v` of the plain form,
    // set_value.cpp): the array or the record with that place changed. The store into a
    // variable's own array or field is the assignment's (see lowerSetIndexInPlace), and this is
    // the same change without a variable to hand over.
    r[":op/set_index"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            if (value == nullptr || value->anyRank() || value->rank() != 1 || c.normCount() != 3) {
                return std::nullopt;
            }
            return std::format(
                "rt::setAt({}, {}, {}, {})",
                c.operand(0),
                c.operand(1),
                c.operand(2),
                c.site(RuntimeDiag::IndexOutOfRange, marks(3)));
        },
        false,
    };
    // The field a store writes is an index, so the record's C++ member name is the type's: the
    // member pointer the runtime takes is that field's (camel/standalone/record.h).
    r[":op/set_field"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const std::string field = c.fieldName(1);
            if (c.normCount() != 3 || field.empty()) {
                return std::nullopt;
            }
            return std::format(
                "rt::setField({}, &{}::{}, {})",
                c.norm(0).text,
                c.cppType(c.normType(0)),
                field,
                c.operand(2));
        },
        false,
    };
    // `xs[positions]` (:op/idx_take, struct.cpp __idx_take_arr__, design principles 5.4): the
    // elements the positions name, in that order. Only a rank-1 value read by rank-1 positions is
    // a flat run here (an rt::Array has no shape, cpp_types.cpp): a value of several dimensions
    // keeps them, and one of an open rank has none, so both run through the bridge. The axis word
    // a call writes names a dimension, and a rank-1 value has the one.
    r[":op/idx_take"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value     = asArrayType(c.normType(0));
            const ArrayType *positions = asArrayType(c.normType(1));
            if (c.normCount() != 2 || value == nullptr || positions == nullptr ||
                value->anyRank() || value->rank() != 1 || positions->anyRank() ||
                positions->rank() != 1 || positions->elemType() == nullptr ||
                positions->elemType()->code() != TypeCode::Int64) {
                return std::nullopt;
            }
            return std::format(
                "rt::take({}, {}, {})",
                c.operand(0),
                c.operand(1),
                c.site(RuntimeDiag::IndexOutOfRange, marks(3)));
        },
        false,
    };
    // The last element (:struct/last_arr, struct.cpp __last_arr__): of the empty array, the fault a
    // read of the position -1 gives, named with the position that is not there.
    r[":struct/last_arr"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            if (c.normCount() != 1 || value == nullptr || value->anyRank() || value->rank() != 1) {
                return std::nullopt;
            }
            return std::format(
                "rt::last({}, {})",
                c.operand(0),
                c.site(RuntimeDiag::IndexOutOfRange, marks(3)));
        },
        false,
    };
    // The reductions and layouts of a module that name their element type in the operator
    // (`array:sum_d`, ...; src/builtin/operators/array_reduce.cpp): one line per element type,
    // because the executor takes exactly one and the name is what picked it. Rank 1 only, as the
    // arrays of this back end are a flat run: a value of several dimensions keeps its own.
    r["array:sum_d"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            if (c.normCount() != 1 || value == nullptr || value->anyRank() || value->rank() != 1 ||
                value->elemType() == nullptr ||
                value->elemType()->code() != TypeCode::Float64) {
                return std::nullopt;
            }
            return std::format("rt::sum({})", c.operand(0));
        },
        false,
    };
    // `array:elements` (src/builtin/operators/array_axis.cpp): the elements in row-major order as
    // an array of one dimension. An rt::Array of this back end is that flat run already -- it keeps
    // no shape (cpp_types.cpp) -- so the value is the one the call was given, whatever rank the
    // type wrote: a rank of several dimensions is a run and nothing more here.
    r["array:elements"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            if (c.normCount() != 1 || asArrayType(c.normType(0)) == nullptr) {
                return std::nullopt;
            }
            return c.norm(0).text;
        },
        false,
    };
    // `array:sort_d` (src/builtin/operators/array.cpp): the same elements in the order min and max
    // use, ties where they stand. A rank-1 run again; the other element types name their own
    // operator and get their own line when a program needs one.
    r["array:sort_d"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            if (c.normCount() != 1 || value == nullptr || value->anyRank() || value->rank() != 1 ||
                value->elemType() == nullptr ||
                value->elemType()->code() != TypeCode::Float64) {
                return std::nullopt;
            }
            return std::format("rt::sorted({})", c.operand(0));
        },
        false,
    };
    // `tensor:reshape` (modules/tensor/kernels/layout.cpp): the same elements in row-major order in
    // another shape. This back end keeps no shape, so a value it hands back is a flat run -- and
    // the shape is observable: printing a rank-2 value gives its rows, `[[1.0, 2.0], [3.0, 4.0]]`,
    // where the run gives `[1.0, 2.0, 3.0, 4.0]`. So only a target of one dimension is faithful
    // here, where the printed form is the run's own; a target of several dimensions is the shape
    // this back end does not carry, and it still runs through the bridge. What is left of the
    // operator is its count check: a target that changes the element count is a fault before
    // anything is read, and the report states the input's own shape.
    r["tensor:reshape"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            if (c.normCount() != 1 || value == nullptr || value->anyRank() || value->rank() != 1) {
                return std::nullopt;
            }
            const ArrayType *target = asArrayType(c.resultType());
            int64_t count           = -1;
            std::string shape;
            if (target != nullptr && !target->anyRank() && target->rank() == 1) {
                const core::type::ExtentForm &extent = target->extents()[0];
                if (extent.isConstant()) {
                    count = extent.constant();
                    shape = std::format("[{}]", count);
                }
            }
            if (count < 0) {
                return c.norm(0).text;
            }
            const std::string site = c.site(
                RuntimeDiag::TensorDimensionMismatch,
                {"reshape",
                 std::format(
                     "reshape changes tensor element count: [{}] -> {}", SiteTable::valueMark(0), shape)});
            return std::format("rt::reshape({}, {}, {})", c.norm(0).text, count, site);
        },
        false,
    };
    // The slice of a one-dimensional array, whichever spelling wrote it (design principles 5.4):
    // the six words are the value, the axis, the two bounds, the step and the mask of the bounds
    // written, and a call that leaves the last two out stands for step 1 and "both written". A
    // value with several dimensions or an open rank is not a flat run here: the bridge runs it.
    r[":op/slice_sub"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const ArrayType *value = asArrayType(c.normType(0));
            if (value == nullptr || value->anyRank() || value->rank() != 1 ||
                c.normCount() < 4 || c.normCount() > 6) {
                return std::nullopt;
            }
            return std::format(
                "rt::sliceSub({}, {}, {}, {}, {}, {}, {}, {})",
                c.operand(0),
                c.operand(1),
                c.operand(2),
                c.operand(3),
                c.normCount() > 4 ? c.operand(4) : std::string("int64_t{1}"),
                c.normCount() > 5 ? c.operand(5) : std::string("int64_t{3}"),
                c.site(RuntimeDiag::SliceOutOfRange, marks(4)),
                c.site(RuntimeDiag::InvalidArgument, marks(2)));
        },
        false,
    };
    // A type name applied to a one-dimensional numeric array converts every element. A value with
    // several dimensions or an open rank is not a flat run here, and neither is bf16 or f16 (this
    // back end has no such element type): those are refused, as the other array operators refuse.
    for (const auto &[uri, element] : {std::pair<const char *, const char *>{":op/atoi", "int32_t"},
                                      {":op/atol", "int64_t"},
                                      {":op/atof", "float"},
                                      {":op/atod", "double"}}) {
        r[uri] = {
            [element](OperatorCall &c) -> std::optional<std::string> {
                const ArrayType *value = asArrayType(c.normType(0));
                if (c.normCount() != 1 || value == nullptr || value->anyRank() ||
                    value->rank() != 1) {
                    return std::nullopt;
                }
                return std::format("rt::convertArray<{}>({})", element, c.operand(0));
            },
            true,
        };
    }
    r[":struct/len_arr"]      = runtimeFunction("length");
    r[":struct/concat_arr"]   = runtimeFunction("concat");
    r[":struct/append_arr"]   = withArgument("append");
    // The loop-state form (Builder::markLoopStateInPlace): writing in place is an optimisation of
    // `append`, and this back end has only the copying form, which gives the same values.
    r[":struct/append_arr_owned"] = withArgument("append");
    // The private copy one state of a loop takes (Builder::markLoopStateInPlace, per state),
    // which the same rewrite puts before the loop: an Array here is a value, and `append` copies
    // a block that another holder shares, so the copy itself has nothing to do (rt::append takes
    // the array by value).
    r[":struct/copy_arr"] = identity();
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
    r["env:get_env"]      = runtimeFunction("getEnv", false);
    // The random module (modules/random): the global stream's two operators and the keyed
    // primitive, through camel/utils/philox.h (standalone/random.h). Keyed random is pure, so its
    // expressions may be inlined and shared (design principles 13.5).
    r["rand:seed"]     = runtimeFunction("randomSeed", false);
    r["rand:next_key"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format("rt::randomNextKey<{}>()", c.cppType(c.resultType()));
        },
        false,
    };
    r["rand:philox"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format(
                "rt::randomPhilox<{}, {}>({}, {})",
                c.cppType(c.resultType()),
                c.cppType(c.normType(0)),
                c.norm(0).text,
                c.operand(1));
        },
        true,
    };
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
    // The complex functions (modules/math/operators.cpp): abs is hypot of the parts, exp is
    // std::exp of a std::complex, conj negates the imaginary part, re and im read one part.
    for (const char *t : {"c", "z"}) {
        r[std::format("math:abs_{}", t)]  = runtimeFunction("complexAbs");
        r[std::format("math:exp_{}", t)]  = cmath("exp");
        r[std::format("math:conj_{}", t)] = cmath("conj");
        r[std::format("math:re_{}", t)]   = runtimeFunction("complexReal");
        r[std::format("math:im_{}", t)]   = runtimeFunction("complexImag");
    }

    // ---- The end of the program (`process::exit(n)`) ----
    for (const char *uri : {"process:exit"}) {
        r[uri] = {
            [](OperatorCall &c) -> std::optional<std::string> {
                return std::format("rt::exit({})", c.norm(0).text);
            },
            false,
        };
    }
    // ---- Enums (:enum/make, :enum/tag, :enum/field; the type carries the construction) ----
    // The tag of a construction and the tag and index of a field read are constants of the
    // program, so the first and second are template arguments of the generated helpers.
    r[":enum/make"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            const Type *result = c.resultType();
            if (result == nullptr || c.normCount() == 0) {
                return std::nullopt;
            }
            const std::string type = c.cppType(result);
            if (type.empty() || type == "void") {
                return std::nullopt;
            }
            std::string fields;
            for (size_t i = 1; i < c.normCount(); ++i) {
                fields += (i > 1 ? ", " : "") + c.norm(i).text;
            }
            return std::format("{}::make<{}>({})", type, c.norm(0).text, fields);
        },
        true,
    };
    r[":enum/tag"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            return std::format("rt::enumTag({})", c.norm(0).text);
        },
        true,
    };
    r[":enum/field"] = {
        [](OperatorCall &c) -> std::optional<std::string> {
            if (c.normCount() != 3) {
                return std::nullopt;
            }
            return std::format(
                "rt::enumField<{}, {}>({})",
                c.norm(0).text,
                c.norm(1).text,
                c.norm(2).text);
        },
        true,
    };

    return r;
}

} // namespace

const OperatorRule *findOperatorRule(std::string_view uri) {
    static const Rules rules = buildRules();
    auto it                  = rules.find(std::string(uri));
    return it == rules.end() ? nullptr : &it->second;
}

} // namespace camel::passes::cpp
