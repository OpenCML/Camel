/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The declared numeric semantics of the `math` module's operators
 * (camel/core/operator_semantics.h, docs/design/op-semantics section 6.2, design principles 7.1),
 * in one table so that every declaration can be read next to the others and checked against the
 * implementations in module.cpp, numeric.cpp and operators.cpp.
 *
 * Design principles 7.1: every transcendental functions declares its error bound in ulps; the
 * square root is correctly rounded; special values are exact. The module does not implement its
 * own algorithms for these: each function forwards to the C++ standard library's `std::` function
 * for the argument's type, so the declared bounds are those of the platform's C library.
 *
 * Where each bound comes from:
 *
 *   exp, log                 2 ulp. Design principles 7.1 sets the standard library target at 2
 *                            ulp for these two; the tensor module declares the same bound
 *                            (modules/tensor/ops/semantics.cpp). The language reference states
 *                            it as well (docs/language/03_data_model.md).
 *   every other Bounded    4 ulp. The C++ standard does not fix the accuracy of the C math
 *   function                functions. Microsoft's UCRT (the implementation on Windows) documents
 *                            "in most cases ... within +/-1 ULP ... though there may be cases
 *                            where there's greater inaccuracy", and results may differ between
 *                            CPUs and Windows versions, so 1 ulp is not a bound. 4 ulp is the
 *                            library target Q4 of docs/design/op-semantics names for the bounded
 *                            functions it discusses (tanh, sigmoid, gelu, erf), and the measured
 *                            worst case of the platform's functions over the inputs this module
 *                            accepts is below it (see handover note ⟨77e9e83⟩ (H4) for the
 *                            measurement).
 *
 * The float32 declarations describe the `_f` overload (compute = f32); the `_d` overload has its
 * own record with compute = f64. `math:sqrt` is one URI for all four numeric types: its record
 * describes the narrow float case (f32, and i32 converted to it) and its algorithm says how the
 * wider cases differ, as the tensor declarations do.
 *
 * The complex functions: `re`, `im` and `conj` are Exact (reading a part and flipping a sign are
 * not arithmetic). `abs` (hypot of the parts) is Bounded by the module's platform bound of 4 ulp.
 * `exp` of a complex number is the platform's std::exp(std::complex), which composes its exp with
 * its sin and cos: the 2 ulp target of the real exp does not describe the composition, so its
 * record declares the platform bound of 4 ulp. Neither `abs` nor `exp` always propagates a NaN
 * (hypot(+inf, NaN) is +inf; cexp(-inf + i*NaN) is 0), as for the real hypot and the C standard's
 * special cases.
 *
 * Integer operators are Exact and sensitive to no permission (two's complement bit operations and
 * integer arithmetic have no rounding). Special values are the IEEE ones unless a row says
 * otherwise (`pow` and `hypot` have standard-mandated results that are not NaN propagation).
 */

#include "semantics.h"

#include "camel/core/operator_table.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
using camel::core::type::TypeCode;

/// The float overloads of the module and the type each computes in.
struct FloatType {
    const char *suffix;
    TypeCode code;
};
constexpr FloatType kFloatTypes[] = {{"_f", TypeCode::Float32}, {"_d", TypeCode::Float64}};

/// approx_math, change_precision and the finite_math assumptions are the permissions that can
/// change a bounded function (design principles 7.2, docs/design/op-semantics 6.2).
constexpr PermissionMask kApprox = kApproxMath | kChangePrecision | kFiniteMath;

/// No rounding, but special values matter: the finite_math assumptions can change the result (a
/// NaN assumed away, the sign of a zero, a flushed subnormal).
NumericSemantics exactFloat(TypeCode compute, std::string algorithm) {
    return NumericSemantics{
        .cls         = NumericClass::Exact,
        .compute     = compute,
        .sensitiveTo = kFiniteMath,
        .algorithm   = std::move(algorithm),
    };
}

/// No arithmetic at all: integer arithmetic (wrapping) and bit operations.
NumericSemantics exactInteger(std::string algorithm) {
    return NumericSemantics{
        .cls         = NumericClass::Exact,
        .sensitiveTo = kNoPermissions,
        .algorithm   = std::move(algorithm),
    };
}

/// A fixed sequence of correctly rounded IEEE steps in `compute`.
NumericSemantics rounded(TypeCode compute, PermissionMask sensitiveTo, std::string algorithm) {
    return NumericSemantics{
        .cls         = NumericClass::CorrectlyRounded,
        .compute     = compute,
        .sensitiveTo = sensitiveTo,
        .algorithm   = std::move(algorithm),
    };
}

/// A math function within `ulp` of the exact result; the special values are exact unless
/// `specials` says otherwise.
NumericSemantics
bounded(TypeCode compute, float ulp, SpecialValues specials, std::string algorithm) {
    return NumericSemantics{
        .cls         = NumericClass::Bounded,
        .ulpBound    = ulp,
        .compute     = compute,
        .specials    = specials,
        .sensitiveTo = kApprox,
        .algorithm   = std::move(algorithm),
    };
}

/// Declares the semantics of `uri`. A rejected declaration is a defect of this module, so the
/// exception names the module (as the tensor module does when its declarations are refused).
void declare(std::string_view uri, NumericSemantics numeric) {
    try {
        OperatorTable::instance().declareSemantics(
            uri,
            OperatorSemantics{.key = OperatorKey::of(uri), .numeric = std::move(numeric)});
    } catch (const std::invalid_argument &e) {
        throw std::logic_error("module math: " + std::string(e.what()));
    }
}

/// Declares `name` for both float widths, the numeric semantics `make` gives each type.
template <class Make> void forFloatWidths(std::string_view name, Make make) {
    for (const FloatType &type : kFloatTypes) {
        declare(std::string("math:") + std::string(name) + type.suffix, make(type.code));
    }
}

/// Declares `name` for the integer widths only.
template <class Make> void forIntegerWidths(std::string_view name, Make make) {
    declare(std::string("math:") + std::string(name) + "_i", make());
    declare(std::string("math:") + std::string(name) + "_l", make());
}

// ---- The bounded functions ------------------------------------------------------------------

/// A unary transcendental: its declared ulp bound and whether a NaN operand gives NaN.
struct BoundedRow {
    const char *name;
    float ulp;
    bool nanPropagates;
    const char *algorithm;
};

/// The unary transcendentals. The bounds come from the comment at the top of this file: 2 ulp for
/// exp and log (the standard library target), 4 ulp for the rest (the platform's documented
/// accuracy is "usually within 1 ulp", not a bound, and the platform may differ between CPUs).
constexpr BoundedRow kUnaryBounded[] = {
    // 4 ulp: the trigonometric functions reduce the argument first; their special values (NaN,
    // +-inf as a domain error, +-0) are exact.
    {"sin",
     4.0f,
     true,
     "sin x per element, within 4 ulp; NaN, +-inf and +-0 exact (an infinite argument is a "
     "domain error: NaN)"},
    {"cos",
     4.0f,
     true,
     "cos x per element, within 4 ulp; NaN, +-inf and +-0 exact (an infinite argument is a "
     "domain error: NaN)"},
    {"tan",
     4.0f,
     true,
     "tan x per element, within 4 ulp; NaN, +-inf and +-0 exact (an infinite argument is a "
     "domain error: NaN)"},
    // 4 ulp: the inverse trigonometric functions; outside [-1, 1] asin and acos give NaN.
    {"asin",
     4.0f,
     true,
     "asin x per element, within 4 ulp; NaN and +-inf exact (outside [-1, 1] is a domain error: "
     "NaN)"},
    {"acos",
     4.0f,
     true,
     "acos x per element, within 4 ulp; NaN and +-inf exact (outside [-1, 1] is a domain error: "
     "NaN)"},
    {"atan", 4.0f, true, "atan x per element, within 4 ulp; NaN, +-inf and +-0 exact"},
    {"sinh", 4.0f, true, "sinh x per element, within 4 ulp; NaN, +-inf and +-0 exact"},
    {"cosh", 4.0f, true, "cosh x per element, within 4 ulp; NaN and +-inf exact"},
    {"tanh", 4.0f, true, "tanh x per element, within 4 ulp; NaN, +-inf and +-0 exact"},
    // 2 ulp: the standard library target for log.
    {"log",
     2.0f,
     true,
     "ln x per element, within 2 ulp; NaN, +-inf, +-0 and x < 0 exact (x = 0 is -inf, x < 0 is "
     "a domain error: NaN)"},
    // 4 ulp: the other logarithms and the near-zero forms, which the target does not name.
    {"log2",
     4.0f,
     true,
     "log2 x per element, within 4 ulp; NaN, +-inf, +-0 and x < 0 exact (x = 0 is -inf, x < 0 "
     "is a domain error: NaN)"},
    {"log10",
     4.0f,
     true,
     "log10 x per element, within 4 ulp; NaN, +-inf, +-0 and x < 0 exact (x = 0 is -inf, x < 0 "
     "is a domain error: NaN)"},
    {"log1p",
     4.0f,
     true,
     "ln(1 + x) per element, within 4 ulp; NaN, +-inf and +-0 exact (x = -1 is -inf, x < -1 is "
     "a domain error: NaN)"},
    {"expm1",
     4.0f,
     true,
     "e^x - 1 per element, within 4 ulp; NaN, +-inf and +-0 exact (e^-inf - 1 is -1)"},
    // 2 ulp: the standard library target for exp.
    {"exp",
     2.0f,
     true,
     "e^x per element, within 2 ulp; NaN, +-inf and +-0 exact (e^-inf is +0, e^+inf is +inf)"},
};

/// The binary transcendentals.
constexpr BoundedRow kBinaryBounded[] = {
    {"atan2",
     4.0f,
     true,
     "atan2(y, x) per element, within 4 ulp; NaN, +-inf, +-0 and the quadrant boundaries "
     "exact"},
    // pow is not NaN propagation: pow(x, 0) and pow(1, y) are 1 even when the other operand is
    // NaN, and a negative base with a non-integer exponent is a domain error (NaN).
    {"pow",
     4.0f,
     false,
     "x^y per element, within 4 ulp; the standard's special cases exact (pow(x, +-0) and "
     "pow(1, y) are 1 even for a NaN operand; a negative base with a non-integer exponent is a "
     "domain error: NaN)"},
    // hypot(+inf, NaN) is +inf by the standard, not NaN: it does not propagate NaN.
    {"hypot",
     4.0f,
     false,
     "sqrt(x^2 + y^2) per element without intermediate overflow or underflow, within 4 ulp; the "
     "standard's special cases exact (an infinite operand gives +inf even when the other is "
     "NaN)"},
};

// ---- The declared table ---------------------------------------------------------------------

void declareElementaryFunctions() {
    for (const FloatType &type : kFloatTypes) {
        // abs: the sign cleared, exact.
        declare(
            std::string("math:abs") + type.suffix,
            exactFloat(type.code, "|x|: the sign cleared; NaN gives NaN, -0 gives +0"));
        // round, ceil, floor and trunc return an integer-valued float: no rounding error.
        declare(
            std::string("math:round") + type.suffix,
            exactFloat(
                type.code,
                "the nearest integer, halfway away from zero, as a float; NaN and +-inf are "
                "unchanged and -0 gives -0"));
        declare(
            std::string("math:ceil") + type.suffix,
            exactFloat(
                type.code,
                "the smallest integer not below x, as a float; NaN and +-inf are unchanged"));
        declare(
            std::string("math:floor") + type.suffix,
            exactFloat(
                type.code,
                "the largest integer not above x, as a float; NaN and +-inf are unchanged"));
        declare(
            std::string("math:trunc") + type.suffix,
            exactFloat(
                type.code,
                "x with its fractional part removed, as a float; NaN and +-inf are unchanged and "
                "+-0 are preserved"));
    }
    // abs, min, max, clamp and sign are overloaded over the four numeric types.
    declare(
        "math:abs_i",
        exactInteger(
            "|x|: the sign cleared; the smallest integer has no positive counterpart, so its "
            "absolute value is not defined (docs/stdlib-reference.md)"));
    declare(
        "math:abs_l",
        exactInteger(
            "|x|: the sign cleared; the smallest integer has no positive counterpart, so its "
            "absolute value is not defined (docs/stdlib-reference.md)"));
    declare("math:min_i", exactInteger("the smaller of two integers (equal values are the same)"));
    declare("math:min_l", exactInteger("the smaller of two integers (equal values are the same)"));
    declare("math:max_i", exactInteger("the larger of two integers (equal values are the same)"));
    declare("math:max_l", exactInteger("the larger of two integers (equal values are the same)"));
    declare(
        "math:clamp_i",
        exactInteger("x bounded to [lo, hi]; lo above hi is a fault (the result is one operand)"));
    declare(
        "math:clamp_l",
        exactInteger("x bounded to [lo, hi]; lo above hi is a fault (the result is one operand)"));
    declare("math:sign_i", exactInteger("-1, 0 or +1 by the sign of x; -0 does not exist here"));
    declare("math:sign_l", exactInteger("-1, 0 or +1 by the sign of x; -0 does not exist here"));

    for (const FloatType &type : kFloatTypes) {
        // min and max are IEEE 754-2019 minimum and maximum (design principles 7.7).
        declare(
            std::string("math:min") + type.suffix,
            exactFloat(
                type.code,
                "IEEE 754-2019 minimum per element: a NaN on either side gives the canonical "
                "quiet NaN, -0 < +0"));
        declare(
            std::string("math:max") + type.suffix,
            exactFloat(
                type.code,
                "IEEE 754-2019 maximum per element: a NaN on either side gives the canonical "
                "quiet NaN, -0 < +0"));
        declare(
            std::string("math:clamp") + type.suffix,
            exactFloat(
                type.code,
                "x bounded to [lo, hi] (one of the operands, no arithmetic): a NaN bound or lo "
                "above hi is a fault; a NaN x gives that NaN"));
        declare(
            std::string("math:sign") + type.suffix,
            exactFloat(
                type.code,
                "-1, 0 or +1 by the sign of x; NaN gives NaN and both zeros give +0"));
        declare(
            std::string("math:is_nan") + type.suffix,
            exactFloat(type.code, "true when x is a NaN, false otherwise (a test, no arithmetic)"));
        declare(
            std::string("math:is_inf") + type.suffix,
            exactFloat(
                type.code,
                "true when x is an infinity, false otherwise (a test, no arithmetic)"));
    }

    // One correctly rounded square root in the width of the argument; an integer argument is
    // converted first (a correctly rounded step too, which `compute` names for the narrow case).
    declare(
        "math:sqrt",
        rounded(
            TypeCode::Float32,
            kApprox,
            "one correctly rounded square root: f32 for a float (f32) or i32 argument, f64 for a "
            "double (f64) or int (i64) argument, an integer converted as a correctly rounded "
            "step first; sqrt(-0) is -0, sqrt(x < 0) is a domain error (NaN), sqrt(+inf) is "
            "+inf"));
}

// ---- The complex functions ------------------------------------------------------------------

/// `re`, `im` and `conj` are Exact: reading one part and flipping one sign are not arithmetic.
/// `abs` is hypot of the parts and `exp` is the platform's std::exp(std::complex); both are
/// Bounded by the platform's accuracy, like the real functions they are built from, and neither
/// always propagates a NaN (hypot(+inf, NaN) is +inf, and cexp(-inf + i*NaN) is 0).
void declareComplexFunctions() {
    declare(
        "math:re_c",
        exactFloat(TypeCode::Float32, "the real part of the value, f32: no arithmetic"));
    declare(
        "math:re_z",
        exactFloat(TypeCode::Float64, "the real part of the value, f64: no arithmetic"));
    declare(
        "math:im_c",
        exactFloat(TypeCode::Float32, "the imaginary part of the value, f32: no arithmetic"));
    declare(
        "math:im_z",
        exactFloat(TypeCode::Float64, "the imaginary part of the value, f64: no arithmetic"));
    declare(
        "math:conj_c",
        exactFloat(
            TypeCode::Float32,
            "the real part and the negated imaginary part, f32; negating a float flips its sign "
            "bit, exact (a NaN part stays a NaN, -0 becomes +0 in the imaginary part)"));
    declare(
        "math:conj_z",
        exactFloat(
            TypeCode::Float64,
            "the real part and the negated imaginary part, f64; negating a float flips its sign "
            "bit, exact (a NaN part stays a NaN, -0 becomes +0 in the imaginary part)"));
    declare(
        "math:abs_c",
        bounded(
            TypeCode::Float32,
            4.0f,
            SpecialValues{.nans = false},
            "sqrt(re^2 + im^2) in f32 without intermediate overflow or underflow (hypot), within "
            "4 ulp; an infinite part gives +inf even when the other is NaN"));
    declare(
        "math:abs_z",
        bounded(
            TypeCode::Float64,
            4.0f,
            SpecialValues{.nans = false},
            "sqrt(re^2 + im^2) in f64 without intermediate overflow or underflow (hypot), within "
            "4 ulp; an infinite part gives +inf even when the other is NaN"));
    declare(
        "math:exp_c",
        bounded(
            TypeCode::Float32,
            4.0f,
            SpecialValues{.nans = false},
            "e^(re+i*im) in f32 as the platform's std::exp(std::complex): its magnitude is its "
            "exp and its direction its sin/cos, so the result is within the platform's accuracy "
            "of that composition (4 ulp), not the 2 ulp target of the real exp alone; the C "
            "standard's special cases are exact (a NaN part is not always propagated: "
            "exp(-inf + NaN i) is 0)"));
    declare(
        "math:exp_z",
        bounded(
            TypeCode::Float64,
            4.0f,
            SpecialValues{.nans = false},
            "e^(re+i*im) in f64 as the platform's std::exp(std::complex): its magnitude is its "
            "exp and its direction its sin/cos, so the result is within the platform's accuracy "
            "of that composition (4 ulp), not the 2 ulp target of the real exp alone; the C "
            "standard's special cases are exact (a NaN part is not always propagated: "
            "exp(-inf + NaN i) is 0)"));
}

void declareBoundedFunctions() {
    for (const BoundedRow &row : kUnaryBounded) {
        forFloatWidths(row.name, [&](TypeCode type) {
            return bounded(type, row.ulp, SpecialValues{.nans = row.nanPropagates}, row.algorithm);
        });
    }
    for (const BoundedRow &row : kBinaryBounded) {
        forFloatWidths(row.name, [&](TypeCode type) {
            return bounded(type, row.ulp, SpecialValues{.nans = row.nanPropagates}, row.algorithm);
        });
    }
}

// ---- Integers and conversions ---------------------------------------------------------------

void declareIntegerFunctions() {
    forIntegerWidths("gcd", [] {
        return exactInteger(
            "the non-negative greatest common divisor; a result that does not fit the type is a "
            "fault");
    });
    forIntegerWidths("lcm", [] {
        return exactInteger(
            "the non-negative least common multiple; a product that does not fit the type is a "
            "fault");
    });
    forIntegerWidths("bit_and", [] { return exactInteger("x & y, two's complement"); });
    forIntegerWidths("bit_or", [] { return exactInteger("x | y, two's complement"); });
    forIntegerWidths("bit_xor", [] { return exactInteger("x ^ y, two's complement"); });
    forIntegerWidths("bit_not", [] { return exactInteger("~x, two's complement"); });
    // shl shifts the unsigned value, so shifting into the sign bit is defined; shr is
    // arithmetic. A count outside 0..width-1 is a fault rather than C++'s undefined behaviour.
    declare(
        "math:shl",
        exactInteger(
            "x << count on the unsigned value (bits shifted out are lost, zeros come in); a "
            "count outside 0..width-1 is a fault"));
    declare(
        "math:shr",
        exactInteger(
            "x >> count, arithmetic (the sign bit is copied in); a count outside 0..width-1 is a "
            "fault"));
    // The base conversions of integers: no arithmetic, only the digits of the value.
    forIntegerWidths("bin", [] {
        return exactInteger(
            "the 0b-prefixed binary digits; a negative value is its 64-bit two's-complement "
            "pattern, leading zeros dropped (0 is 0b0), exact");
    });
    forIntegerWidths("oct", [] {
        return exactInteger("the 0o-prefixed octal digits; a negative value keeps its sign, exact");
    });
    forIntegerWidths("hex", [] {
        return exactInteger(
            "the 0x-prefixed uppercase hexadecimal digits; a negative value keeps its sign, exact");
    });
}

} // namespace

void declareMathOperatorSemantics() {
    declareElementaryFunctions();
    declareComplexFunctions();
    declareBoundedFunctions();
    declareIntegerFunctions();
}