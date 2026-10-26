// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The elementary functions, the constants, integer helpers and the bit operations of the
 * `math` module. All are primitives written in C++ and pure.
 *
 * Numeric types are not converted implicitly (design principles 5.1): a function over floats
 * takes `f32` or `float` and returns the type it was given; a function of two arguments takes
 * two arguments of one type.
 *
 * Bit operations are functions, not operators: the meaning of `&`, `|`, `^`, `&=` and `|=` in
 * the grammar is not ruled on.
 */

#include "numeric.h"
#include "half.h"
#include "minmax.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <optional>

using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::math;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

[[noreturn]] void unsupported(const char *name, TypeCode code, const char *wanted) {
    throwRuntimeFault(
        RuntimeDiag::OperandTypeUnsupported,
        std::string("math.") + name,
        typeCodeToString(code),
        wanted);
}

[[noreturn]] void invalid(const char *name, std::string why) {
    throwRuntimeFault(RuntimeDiag::InvalidArgument, std::string("math.") + name, std::move(why));
}

// ---- float arguments: (f32) => f32 and (float) => float ----

#define CAMEL_MATH_FN(NAME)                                                                            slot_t __math_##NAME##__(ArgsView &, ArgsView &norm, Context &) {                                      switch (norm.code(0)) {                                                                            case TypeCode::Float32:                                                                                return toSlot(static_cast<Float32>(std::NAME(norm.get<Float32>(0))));                          case TypeCode::Float64:                                                                                return toSlot(static_cast<Float64>(std::NAME(norm.get<Float64>(0))));                          default:                                                                                               unsupported(#NAME, norm.code(0), "a float");                                                   }                                                                                              }
#define CAMEL_MATH_FN2(NAME)                                                                           slot_t __math_##NAME##__(ArgsView &, ArgsView &norm, Context &) {                                      switch (norm.code(0)) {                                                                            case TypeCode::Float32:                                                                                return toSlot(                                                                                         static_cast<Float32>(std::NAME(norm.get<Float32>(0), norm.get<Float32>(1))));              case TypeCode::Float64:                                                                                return toSlot(                                                                                         static_cast<Float64>(std::NAME(norm.get<Float64>(0), norm.get<Float64>(1))));              default:                                                                                               unsupported(#NAME, norm.code(0), "a float");                                                   }                                                                                              }

CAMEL_MATH_FN(sin)
CAMEL_MATH_FN(cos)
CAMEL_MATH_FN(tan)
CAMEL_MATH_FN(asin)
CAMEL_MATH_FN(acos)
CAMEL_MATH_FN(atan)
CAMEL_MATH_FN(sinh)
CAMEL_MATH_FN(cosh)
CAMEL_MATH_FN(tanh)
CAMEL_MATH_FN(log)
CAMEL_MATH_FN(log2)
CAMEL_MATH_FN(log10)
CAMEL_MATH_FN(log1p)
CAMEL_MATH_FN(expm1)
CAMEL_MATH_FN(trunc)
CAMEL_MATH_FN2(atan2)
CAMEL_MATH_FN2(pow)
CAMEL_MATH_FN2(hypot)

#undef CAMEL_MATH_FN
#undef CAMEL_MATH_FN2

// ---- min, max, clamp, sign: the four numeric types ----

// min and max are IEEE 754-2019 minimum and maximum (design principles 7.7), as the tensor
// operators are (modules/tensor/kernels/minmax.h) and as the bf16 and f16 overloads are
// (operators.cpp): the shared comparison is minmax.h, so there is one statement of it.

template <typename T> T clampOf(T x, T lo, T hi) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(lo) || std::isnan(hi))
            invalid("clamp", "a bound is NaN");
    }
    if (hi < lo)
        invalid("clamp", std::format("the lower bound {} is above the upper bound {}", lo, hi));
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(x))
            return x;
    }
    return x < lo ? lo : (hi < x ? hi : x);
}

template <typename T> T signOf(T x) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(x))
            return x;
    }
    return static_cast<T>((T(0) < x) - (x < T(0)));
}

#define CAMEL_MATH_NUMERIC(NAME, EXPR)                                                             \
    slot_t __math_##NAME##__(ArgsView &, ArgsView &norm, Context &) {                              \
        switch (norm.code(0)) {                                                                    \
        case TypeCode::Int32: {                                                                    \
            using T = Int32;                                                                       \
            return toSlot(static_cast<T>(EXPR));                                                   \
        }                                                                                          \
        case TypeCode::Int64: {                                                                    \
            using T = Int64;                                                                       \
            return toSlot(static_cast<T>(EXPR));                                                   \
        }                                                                                          \
        case TypeCode::Float32: {                                                                  \
            using T = Float32;                                                                     \
            return toSlot(static_cast<T>(EXPR));                                                   \
        }                                                                                          \
        case TypeCode::Float64: {                                                                  \
            using T = Float64;                                                                     \
            return toSlot(static_cast<T>(EXPR));                                                   \
        }                                                                                          \
        default:                                                                                   \
            unsupported(#NAME, norm.code(0), "a number");                                          \
        }                                                                                          \
    }

// `min` and `max` are over every ordered type, not only the numbers: the numbers are IEEE
// 754-2019 minimum and maximum, `string` is ordered by its bytes (a proper prefix first, as `<`
// compares strings; design principles log 2.9) and `bool` has false below true. `is_ordered` is
// the condition that admits exactly these types, so a declaration guarded by it may call these.
slot_t minOrMax(ArgsView &norm, bool minimum) {
    switch (norm.code(0)) {
    case TypeCode::Int32: {
        const Int32 a = norm.get<Int32>(0), b = norm.get<Int32>(1);
        return toSlot(minimum ? minOf(a, b) : maxOf(a, b));
    }
    case TypeCode::Int64: {
        const Int64 a = norm.get<Int64>(0), b = norm.get<Int64>(1);
        return toSlot(minimum ? minOf(a, b) : maxOf(a, b));
    }
    case TypeCode::Float32: {
        const Float32 a = norm.get<Float32>(0), b = norm.get<Float32>(1);
        return toSlot(minimum ? minOf(a, b) : maxOf(a, b));
    }
    case TypeCode::Float64: {
        const Float64 a = norm.get<Float64>(0), b = norm.get<Float64>(1);
        return toSlot(minimum ? minOf(a, b) : maxOf(a, b));
    }
    case TypeCode::String: {
        String *a = norm.get<String *>(0), *b = norm.get<String *>(1);
        const int order = a->compare(b);
        return toSlot(minimum ? (order <= 0 ? a : b) : (order >= 0 ? a : b));
    }
    case TypeCode::Bool: {
        const Bool a = norm.get<Bool>(0), b = norm.get<Bool>(1);
        return toSlot(static_cast<Bool>(minimum ? (a && b) : (a || b)));
    }
    default:
        unsupported(minimum ? "min" : "max", norm.code(0), "an ordered value");
    }
}

slot_t __math_min__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMax(norm, true);
}

slot_t __math_max__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMax(norm, false);
}

CAMEL_MATH_NUMERIC(clamp, clampOf<T>(norm.get<T>(0), norm.get<T>(1), norm.get<T>(2)))
CAMEL_MATH_NUMERIC(sign, signOf<T>(norm.get<T>(0)))

#undef CAMEL_MATH_NUMERIC

slot_t __math_is_nan__(ArgsView &, ArgsView &norm, Context &) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(static_cast<Bool>(std::isnan(norm.get<Float32>(0))));
    case TypeCode::Float64:
        return toSlot(static_cast<Bool>(std::isnan(norm.get<Float64>(0))));
    default:
        unsupported("is_nan", norm.code(0), "a float");
    }
}

slot_t __math_is_inf__(ArgsView &, ArgsView &norm, Context &) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(static_cast<Bool>(std::isinf(norm.get<Float32>(0))));
    case TypeCode::Float64:
        return toSlot(static_cast<Bool>(std::isinf(norm.get<Float64>(0))));
    default:
        unsupported("is_inf", norm.code(0), "a float");
    }
}

// The constants (`pi`, `e`, `inf`, `nan`) are values the module exports, not operators: see
// MathModule::MathModule (design principles 10.1).

// ---- gcd, lcm ----
//
// The result is never negative. A result that does not fit the type (gcd of the smallest
// integer with itself or with 0, an lcm that overflows) is a fault.

template <typename T> T gcdOf(T a, T b) {
    using U = std::make_unsigned_t<T>;
    // The magnitude of the smallest integer fits the unsigned type.
    const U ua = a < 0 ? U(0) - static_cast<U>(a) : static_cast<U>(a);
    const U ub = b < 0 ? U(0) - static_cast<U>(b) : static_cast<U>(b);
    const U g  = std::gcd(ua, ub);
    if (g > static_cast<U>(std::numeric_limits<T>::max()))
        invalid("gcd", std::format("the result {} does not fit the integer type", g));
    return static_cast<T>(g);
}

template <typename T> T lcmOf(T a, T b) {
    using U = std::make_unsigned_t<T>;
    if (a == 0 || b == 0)
        return 0;
    const U ua = a < 0 ? U(0) - static_cast<U>(a) : static_cast<U>(a);
    const U ub = b < 0 ? U(0) - static_cast<U>(b) : static_cast<U>(b);
    const U q  = ua / std::gcd(ua, ub);
    const U limit = static_cast<U>(std::numeric_limits<T>::max());
    if (q > limit / ub)
        invalid("lcm", std::format("the least common multiple of {} and {} overflows", a, b));
    return static_cast<T>(q * ub);
}

#define CAMEL_MATH_INTEGER(NAME, EXPR)                                                             \
    slot_t __math_##NAME##__(ArgsView &, ArgsView &norm, Context &) {                              \
        switch (norm.code(0)) {                                                                    \
        case TypeCode::Int32: {                                                                    \
            using T = Int32;                                                                       \
            using U = uint32_t;                                                                    \
            (void)sizeof(U);                                                                       \
            return toSlot(static_cast<T>(EXPR));                                                   \
        }                                                                                          \
        case TypeCode::Int64: {                                                                    \
            using T = Int64;                                                                       \
            using U = uint64_t;                                                                    \
            (void)sizeof(U);                                                                       \
            return toSlot(static_cast<T>(EXPR));                                                   \
        }                                                                                          \
        default:                                                                                   \
            unsupported(#NAME, norm.code(0), "an integer");                                        \
        }                                                                                          \
    }

CAMEL_MATH_INTEGER(gcd, gcdOf<T>(norm.get<T>(0), norm.get<T>(1)))
CAMEL_MATH_INTEGER(lcm, lcmOf<T>(norm.get<T>(0), norm.get<T>(1)))

// ---- bit operations (two's complement) ----

CAMEL_MATH_INTEGER(bit_and, norm.get<T>(0) & norm.get<T>(1))
CAMEL_MATH_INTEGER(bit_or, norm.get<T>(0) | norm.get<T>(1))
CAMEL_MATH_INTEGER(bit_xor, norm.get<T>(0) ^ norm.get<T>(1))
CAMEL_MATH_INTEGER(bit_not, ~norm.get<T>(0))

// The number of positions, which may be given as either integer type. A count that is
// negative or not below the width of the value is a fault: C++ leaves it undefined, and
// machines disagree on it.
Int64 shiftCount(const char *name, ArgsView &norm, int width) {
    Int64 count = 0;
    switch (norm.code(1)) {
    case TypeCode::Int32:
        count = norm.get<Int32>(1);
        break;
    case TypeCode::Int64:
        count = norm.get<Int64>(1);
        break;
    default:
        unsupported(name, norm.code(1), "an integer");
    }
    if (count < 0 || count >= width) {
        invalid(
            name,
            std::format("the shift count {} is not in 0..{} for a {}-bit integer", count, width - 1, width));
    }
    return count;
}

// shl: bits shifted out are lost, zeros come in (the shift is done on the unsigned value, so
// shifting into the sign bit is defined). shr: arithmetic, the sign bit is copied in.
CAMEL_MATH_INTEGER(
    shl, static_cast<U>(norm.get<T>(0)) << shiftCount("shl", norm, static_cast<int>(sizeof(T) * 8)))
CAMEL_MATH_INTEGER(
    shr, norm.get<T>(0) >> shiftCount("shr", norm, static_cast<int>(sizeof(T) * 8)))

#undef CAMEL_MATH_INTEGER

// The shifts of the declarations that give the count in angle brackets (`x->shl<count>`): the
// count is the one `with` argument, and the same fault as above refuses one outside 0..width-1.
template <typename T> slot_t shiftByWith(bool left, const char *name, ArgsView &with, ArgsView &norm) {
    using U           = std::make_unsigned_t<T>;
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    const Int64 count = with.get<Int64>(0);
    if (count < 0 || count >= bits) {
        invalid(
            name,
            std::format("the shift count {} is not in 0..{} for a {}-bit integer", count, bits - 1, bits));
    }
    const T x = norm.get<T>(0);
    return toSlot(
        left ? static_cast<T>(static_cast<U>(x) << count) : static_cast<T>(x >> count));
}

slot_t __math_shl_i__(ArgsView &with, ArgsView &norm, Context &) {
    return shiftByWith<Int32>(true, "shl", with, norm);
}
slot_t __math_shl_l__(ArgsView &with, ArgsView &norm, Context &) {
    return shiftByWith<Int64>(true, "shl", with, norm);
}
slot_t __math_shr_i__(ArgsView &with, ArgsView &norm, Context &) {
    return shiftByWith<Int32>(false, "shr", with, norm);
}
slot_t __math_shr_l__(ArgsView &with, ArgsView &norm, Context &) {
    return shiftByWith<Int64>(false, "shr", with, norm);
}

// The square root in f64, the kernel every float type goes through: the operand widens to f64
// exactly and the result is rounded once into the type asked for. sqrt(-0) is -0 and sqrt of a
// negative number is NaN (the C library's, which is IEEE 754's).
slot_t __math_sqrt_d__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(std::sqrt(norm.get<Float64>(0)));
}

} // namespace

std::unordered_map<std::string, operator_t> getMathNumericOpsMap() {
    std::unordered_map<std::string, operator_t> ops;
    const auto both = [&](const std::string &name, operator_t op) {
        ops[name + "_f"] = op;
        ops[name + "_d"] = op;
    };
    const auto integers = [&](const std::string &name, operator_t op) {
        ops[name + "_i"] = op;
        ops[name + "_l"] = op;
    };
    const auto numbers = [&](const std::string &name, operator_t op) {
        both(name, op);
        integers(name, op);
    };
    both("sin", __math_sin__);
    both("cos", __math_cos__);
    both("tan", __math_tan__);
    both("asin", __math_asin__);
    both("acos", __math_acos__);
    both("atan", __math_atan__);
    both("sinh", __math_sinh__);
    both("cosh", __math_cosh__);
    both("tanh", __math_tanh__);
    both("log", __math_log__);
    both("log2", __math_log2__);
    both("log10", __math_log10__);
    both("log1p", __math_log1p__);
    both("expm1", __math_expm1__);
    both("trunc", __math_trunc__);
    both("atan2", __math_atan2__);
    both("pow", __math_pow__);
    both("hypot", __math_hypot__);
    numbers("min", __math_min__);
    numbers("max", __math_max__);
    // `min`/`max` also take the other ordered types (`is_ordered` admits them): strings by their
    // bytes and bools with false below true.
    ops["min_s"] = __math_min__;
    ops["max_s"] = __math_max__;
    ops["min_b"] = __math_min__;
    ops["max_b"] = __math_max__;
    // The two reduced-precision floating types take the same IEEE 754-2019 minimum and maximum,
    // computed on the value each operand widens to exactly (operators.cpp).
    ops["min_bf"] = __math_min_bf__;
    ops["max_bf"] = __math_max_bf__;
    ops["min_f6"] = __math_min_f6__;
    ops["max_f6"] = __math_max_f6__;
    numbers("clamp", __math_clamp__);
    numbers("sign", __math_sign__);
    both("is_nan", __math_is_nan__);
    both("is_inf", __math_is_inf__);
    integers("gcd", __math_gcd__);
    integers("lcm", __math_lcm__);
    integers("bit_and", __math_bit_and__);
    integers("bit_or", __math_bit_or__);
    integers("bit_xor", __math_bit_xor__);
    integers("bit_not", __math_bit_not__);
    ops["shl"] = __math_shl__;
    ops["shr"] = __math_shr__;
    ops["shl_i"]   = __math_shl_i__;
    ops["shl_l"]   = __math_shl_l__;
    ops["shr_i"]   = __math_shr_i__;
    ops["shr_l"]   = __math_shr_l__;
    ops["sqrt_d"]  = __math_sqrt_d__;
    return ops;
}
