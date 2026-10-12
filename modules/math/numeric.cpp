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
 * Author: Camel Contributors
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The elementary functions, the constants, integer helpers and the bit operations of the
 * `math` module. All are primitives written in C++ and pure.
 *
 * Numeric types are not converted implicitly (design principles 2.7): a function over floats
 * takes `f32` or `float` and returns the type it was given; a function of two arguments takes
 * two arguments of one type.
 *
 * Bit operations are functions, not operators: the meaning of `&`, `|`, `^`, `&=` and `|=` in
 * the grammar is not ruled on.
 */

#include "numeric.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>

using namespace camel::core::context;
using namespace camel::core::error;
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

// A NaN argument gives NaN, whichever side it is on (std::min and std::max depend on the order).
template <typename T> T minOf(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();
    }
    return b < a ? b : a;
}

template <typename T> T maxOf(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();
    }
    return a < b ? b : a;
}

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

CAMEL_MATH_NUMERIC(min, minOf<T>(norm.get<T>(0), norm.get<T>(1)))
CAMEL_MATH_NUMERIC(max, maxOf<T>(norm.get<T>(0), norm.get<T>(1)))
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

// ---- constants ----
//
// DESIGN-QUESTION(constants): a module cannot export a value (only functions, operators and
// types), so a constant is a pure function of no arguments, `pi()`. std::opt folds the call.

slot_t __math_pi__(ArgsView &, ArgsView &, Context &) { return toSlot(std::numbers::pi); }
slot_t __math_e__(ArgsView &, ArgsView &, Context &) { return toSlot(std::numbers::e); }
slot_t __math_inf__(ArgsView &, ArgsView &, Context &) {
    return toSlot(std::numeric_limits<Float64>::infinity());
}
slot_t __math_nan__(ArgsView &, ArgsView &, Context &) {
    return toSlot(std::numeric_limits<Float64>::quiet_NaN());
}

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

// ---- declarations ----

using param_list_t = std::initializer_list<std::pair<Type *, bool>>;

resolver_ptr_t sig(const param_list_t &norm, Type *result) {
    return StaticFuncTypeResolver::create({}, norm, result);
}

// name: (f32) => f32 and (float) => float
oper_group_ptr_t floatUnary(const std::string &name) {
    Type *f = Type::Float32(), *d = Type::Float64();
    return OperatorGroup::create(
        name,
        {{"math:" + name + "_f", sig({{f, false}}, f)}, {"math:" + name + "_d", sig({{d, false}}, d)}});
}

oper_group_ptr_t floatBinary(const std::string &name) {
    Type *f = Type::Float32(), *d = Type::Float64();
    return OperatorGroup::create(
        name,
        {{"math:" + name + "_f", sig({{f, false}, {f, false}}, f)},
         {"math:" + name + "_d", sig({{d, false}, {d, false}}, d)}});
}

oper_group_ptr_t floatTest(const std::string &name) {
    Type *f = Type::Float32(), *d = Type::Float64();
    return OperatorGroup::create(
        name,
        {{"math:" + name + "_f", sig({{f, false}}, Type::Bool())},
         {"math:" + name + "_d", sig({{d, false}}, Type::Bool())}});
}

// name over `arity` arguments of one type, for the given types (suffixes i, l, f, d).
oper_group_ptr_t sameType(const std::string &name, size_t arity, bool floats) {
    std::vector<std::pair<std::string, resolver_ptr_t>> overloads;
    const std::vector<std::pair<const char *, Type *>> types = {
        {"_i", Type::Int32()},
        {"_l", Type::Int64()},
        {"_f", Type::Float32()},
        {"_d", Type::Float64()},
    };
    for (size_t k = 0; k < types.size(); ++k) {
        if (!floats && k >= 2)
            break;
        Type *t = types[k].second;
        resolver_ptr_t resolver;
        switch (arity) {
        case 1:
            resolver = sig({{t, false}}, t);
            break;
        case 2:
            resolver = sig({{t, false}, {t, false}}, t);
            break;
        default:
            resolver = sig({{t, false}, {t, false}, {t, false}}, t);
            break;
        }
        overloads.push_back({"math:" + name + types[k].first, std::move(resolver)});
    }
    return OperatorGroup::create(name, std::move(overloads));
}

oper_group_ptr_t constant(const std::string &name) {
    return OperatorGroup::create(name, {{"math:" + name, sig({}, Type::Float64())}});
}

oper_group_ptr_t shift(const std::string &name) {
    return OperatorGroup::create(
        name,
        {{"math:" + name,
          DynamicFuncTypeResolver::create(
              {{0, {}}, {2, {false, false}}},
              "(x: i32 | int, count: i32 | int) => i32 | int",
              [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                  -> std::optional<Type *> {
                  const auto integer = [](Type *t) {
                      return t->code() == TypeCode::Int32 || t->code() == TypeCode::Int64;
                  };
                  if (!integer(norm[0]) || !integer(norm[1]))
                      return std::nullopt;
                  return norm[0];
              })}});
}

} // namespace

const std::vector<oper_group_ptr_t> &getMathNumericGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        floatUnary("sin"),      floatUnary("cos"),     floatUnary("tan"),
        floatUnary("asin"),     floatUnary("acos"),    floatUnary("atan"),
        floatUnary("sinh"),     floatUnary("cosh"),    floatUnary("tanh"),
        floatUnary("log"),      floatUnary("log2"),    floatUnary("log10"),
        floatUnary("log1p"),    floatUnary("expm1"),   floatUnary("trunc"),
        floatBinary("atan2"),   floatBinary("pow"),    floatBinary("hypot"),
        sameType("min", 2, true), sameType("max", 2, true), sameType("clamp", 3, true),
        sameType("sign", 1, true),
        floatTest("is_nan"),    floatTest("is_inf"),
        constant("pi"),         constant("e"),         constant("inf"),
        constant("nan"),
        sameType("gcd", 2, false),     sameType("lcm", 2, false),
        sameType("bit_and", 2, false), sameType("bit_or", 2, false),
        sameType("bit_xor", 2, false), sameType("bit_not", 1, false),
        shift("shl"),           shift("shr"),
    };
    return groups;
}

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
    numbers("clamp", __math_clamp__);
    numbers("sign", __math_sign__);
    both("is_nan", __math_is_nan__);
    both("is_inf", __math_is_inf__);
    ops["pi"]  = __math_pi__;
    ops["e"]   = __math_e__;
    ops["inf"] = __math_inf__;
    ops["nan"] = __math_nan__;
    integers("gcd", __math_gcd__);
    integers("lcm", __math_lcm__);
    integers("bit_and", __math_bit_and__);
    integers("bit_or", __math_bit_or__);
    integers("bit_xor", __math_bit_xor__);
    integers("bit_not", __math_bit_not__);
    ops["shl"] = __math_shl__;
    ops["shr"] = __math_shr__;
    return ops;
}
