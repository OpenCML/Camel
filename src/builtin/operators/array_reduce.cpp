// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The native kernels of the `array` module's reductions: `sum`, `min` and `max` over an array
 * (stdlib/array.cml declares them with `#op`, and each is the executor of the Camel function
 * beside it, which stays the semantics).
 *
 * The ruling docs/handover/reviews/questions-2026-10-23.md, section
 * "array-kernel-executors（`array::sum`/`min`/`max` 的执行器）", chose exactly this and says what it
 * is: "`array:sum`/`min`/`max` 是数组归约的实现原子，与 `math:min_bf` 一类标量二元运算不是同一个
 * 东西——后者算两个数，前者沿一个数组做一遍归约（含 f64 累加、舍入一次，设计原则 7.7）。数组模块
 * 自己的归约内核放在 `array` 的执行器组里，是 10.4 说的"热点定义带 `#impl` 内核"的标准形态，不是
 * 重复。" A reduction sweeps the array and accumulates; a scalar binary combines two values. They
 * are different operations, and the executor of a reduction belongs to the module that declares it
 * (design principles 10.4; the ruling also refused the alternative of an `array` kernel naming
 * `math`'s operator, and the alternative of leaving `sum` without a kernel).
 *
 * The comparison and the summation are written once, as the ruling requires
 * ("比较与求和的算式只写一份：`array:*` 的执行器调用 `modules/math/minmax.h` 里已有的模板"), so
 * "how to compare two bf16s" is stated in one place in C++ and the tensor kernels read the same
 * header.
 *
 * One executor per reduction, not one per elementary type: the kernel declaration in
 * stdlib/array.cml is written with the function's own type parameter (`sum_kernel::<T>`), so the
 * instance that reaches a call is the one that says what T is, and the executor answers on that
 * element type here. The arms cover exactly the element types the function is declared for, so
 * every instance of the declaration has an arm and no instance falls back: `sum` takes
 * `is_numeric::<T>()` (i32, int, f32, float, bf16, f16 — the complex types are numeric but their
 * `+` is not this module's, so `sum` is not declared for them) and `min`/`max` take
 * `is_ordered::<T>()` (those six plus bool and string). An element type no arm names — a type
 * another module's `array:min` declaration would have to bring — is not reachable from these
 * declarations; were it reached, the kernel would be wrong, not the body.
 *
 * What each kernel computes is what the Camel function beside it says (the body is the
 * semantics):
 *
 *   - `sum`: an integer element type accumulates in itself and wraps on overflow, as `+` does on
 *     it (i32 sums in i32, i64 in i64); a floating-point element type accumulates in f64 and is
 *     rounded once into the element type at the end (design principles 7.3), which is what
 *     stdlib/array.cml's `sumF64` plus its one conversion spell out. bf16 and f16 are
 *     floating-point element types of that same kind, and reach the executor's f64 arm;
 *   - `min` and `max`: no accumulator and no accumulation width take part. The fold carries an
 *     element of T and combines it with `math::min`/`math::max`, which is IEEE 754-2019 minimum
 *     and maximum — exactly associative and commutative (design principles 7.7) — so the order
 *     does not change the bits and there is nothing to round. The result is `T?`: null for an
 *     empty array.
 */

#include "array.h"

#include "camel/core/operator.h"
#include "camel/core/rtdata.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/type.h"
#include "camel/utils/type.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "modules/math/minmax.h"

#include <cstdint>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using camel::core::type::TypeCode;
using camel::core::type::ArrayType;

namespace {

/// A non-null `T?`: the one-slot tuple of the value, as tagged.h builds it. The value is a leaf
/// of an element type (a number, or a two-byte pattern), so no reference write barrier is needed.
slot_t some(slot_t value) {
    Tuple *box = Tuple::create(1, mm::autoSpace());
    box->set<slot_t>(0, value);
    return toSlot(box);
}

/// The array of the first normal argument.
Array *arrayOf(ArgsView &norm) { return norm.get<Array *>(0); }

/// The element type code of an array argument: what picks the arm below, exactly as the body's
/// `match type T` picks its arm.
TypeCode codeOf(ArgsView &norm) {
    return tt::as_ptr<ArrayType>(norm.type(0))->elemTypeCode();
}

/// The sum of the elements, from +0 in ascending order.
template <typename T> T sumOf(Array *xs) {
    if constexpr (std::is_floating_point_v<T>) {
        double acc = 0.0;
        for (size_t i = 0; i < xs->size(); ++i)
            acc += static_cast<double>(xs->get<T>(i));
        return static_cast<T>(acc);
    } else {
        T acc{};
        for (size_t i = 0; i < xs->size(); ++i)
            acc = static_cast<T>(acc + xs->get<T>(i));
        return acc;
    }
}

/// The sum of the elements of a bf16 or an f16 array: the f64 sum of the exact f32 values,
/// rounded once into the element type (`halfFromValue` is the one conversion, not the
/// f32-mediated `explicit` constructor of half.h).
template <typename Half> slot_t sumOfHalf(Array *xs) {
    double acc = 0.0;
    for (size_t i = 0; i < xs->size(); ++i)
        acc += static_cast<double>(halfToFloat<Half>(xs->getSlot(i)));
    return halfFromValue<Half>(acc);
}

/// The `min` (minimum = true) or `max` of the elements, or null for an empty array.
template <typename T> slot_t minOrMaxOf(Array *xs, bool minimum) {
    if (xs->size() == 0)
        return NullSlot;
    T best = xs->get<T>(0);
    for (size_t i = 1; i < xs->size(); ++i) {
        const T x = xs->get<T>(i);
        best      = minimum ? camel::core::math::minOf(best, x) : camel::core::math::maxOf(best, x);
    }
    return some(toSlot(best));
}

/// The same for a `bool` array: `false` is below `true` (the order `math`'s `min_b`/`max_b`
/// state), which is the order of the two as integers.
template <> slot_t minOrMaxOf<Bool>(Array *xs, bool minimum) {
    if (xs->size() == 0)
        return NullSlot;
    bool best = xs->get<Bool>(0);
    for (size_t i = 1; i < xs->size(); ++i) {
        const bool x = xs->get<Bool>(i);
        if (minimum ? (x < best) : (best < x))
            best = x;
    }
    return some(toSlot(static_cast<Bool>(best)));
}

/// The same for a bf16 or an f16 array: the comparison is the one the header states, on the f32
/// each element widens to. The fold carries the winning *element*, as the body's `reduce` does:
/// the value it carries is an element of the array, so the result is that element's own pattern —
/// the widening is exact and choosing between the two operands is all a minimum does. A NaN
/// operand wins and the first NaN wins over a later one, which is what `reduce` does too.
///
/// Two elements that compare equal are identical except for the sign of zero, and there the two
/// operations are not each other's by exchanging the operands: IEEE 754-2019 says minimum of a
/// mixed pair is -0 and maximum of it is +0, whichever way round they sit. So the tie is decided
/// by the same rule the header applies to its two operands — the sign bits OR-ed for minimum and
/// AND-ed for maximum — and not by comparing the elements or their slots, which would pick
/// whichever of the two the array happens to hold first.
template <typename Half> slot_t minOrMaxOfHalf(Array *xs, bool minimum) {
    if (xs->size() == 0)
        return NullSlot;
    size_t best = 0;
    for (size_t i = 1; i < xs->size(); ++i) {
        const float a = halfToFloat<Half>(xs->getSlot(best));
        const float b = halfToFloat<Half>(xs->getSlot(i));
        if (a != a) {
            continue; // the first NaN wins, as `reduce` keeps it
        }
        if (b != b) {
            best = i;
            continue;
        }
        if (a != b) {
            const float r = minimum ? camel::core::math::minOf(a, b) : camel::core::math::maxOf(a, b);
            if (r != a)
                best = i;
            continue;
        }
        // Equal: only the sign of a zero is left. The winner is -0 for a minimum that has one and
        // +0 for a maximum that has one, so take the element whose sign bit is the combined one
        // (and keep the current one when the two agree).
        const uint32_t signA = std::bit_cast<uint32_t>(a) & 0x80000000u;
        const uint32_t signB = std::bit_cast<uint32_t>(b) & 0x80000000u;
        const uint32_t want  = minimum ? (signA | signB) : (signA & signB);
        if (signB == want && signA != want)
            best = i;
    }
    return some(xs->getSlot(best));
}

/// The same for a `string` array: the comparison is the one `math`'s `min_s`/`max_s` state, on the
/// bytes of the two values, and the fold carries the winning **element**, so the result is that
/// element's own string.
slot_t minOrMaxOfString(Array *xs, bool minimum) {
    if (xs->size() == 0)
        return NullSlot;
    size_t best = 0;
    for (size_t i = 1; i < xs->size(); ++i) {
        const String *a = xs->get<String *>(best);
        const String *b = xs->get<String *>(i);
        if (minimum ? *b < *a : *a < *b)
            best = i;
    }
    return some(xs->getSlot(best));
}

/// One arm per element type: what the reduction does on it. No arm means no kernel (the body
/// runs), which is why every arm is a type the Camel body has an arm for.
template <typename T> struct Reduction {
    static slot_t sum(Array *xs) { return toSlot(sumOf<T>(xs)); }
    static slot_t min(Array *xs) { return minOrMaxOf<T>(xs, true); }
    static slot_t max(Array *xs) { return minOrMaxOf<T>(xs, false); }
};

template <> struct Reduction<Bool> {
    static slot_t min(Array *xs) { return minOrMaxOf<Bool>(xs, true); }
    static slot_t max(Array *xs) { return minOrMaxOf<Bool>(xs, false); }
};

template <> struct Reduction<BFloat16> {
    static slot_t sum(Array *xs) { return sumOfHalf<BFloat16>(xs); }
    static slot_t min(Array *xs) { return minOrMaxOfHalf<BFloat16>(xs, true); }
    static slot_t max(Array *xs) { return minOrMaxOfHalf<BFloat16>(xs, false); }
};

template <> struct Reduction<Float16> {
    static slot_t sum(Array *xs) { return sumOfHalf<Float16>(xs); }
    static slot_t min(Array *xs) { return minOrMaxOfHalf<Float16>(xs, true); }
    static slot_t max(Array *xs) { return minOrMaxOfHalf<Float16>(xs, false); }
};

} // namespace

// The kernels of the three reductions. Which element types may reach them is the kernel
// declarations' own `when` in stdlib/array.cml (`is_integer || is_float` for `sum`, plus
// `is_string` and `is_bool` for `min` and `max`), evaluated by the compiler when an instance is
// made (Builder::declaredRuleApplies): an element type the condition refuses has no executor and
// runs the Camel body, which is the semantics (design principles 10.4; H228: one operator,
// per-type executors).
//
// The one arm per element type is the accumulator or the carrier of the fold, not the rule: `sum`
// adds in f64 and rounds once for a floating-point element type and wraps in the element type
// itself for an integer one, and the two reduced-precision types get their own converter. The
// comparison and the addition are not stated here: `min`/`max` return one of their operands as
// IEEE 754-2019 says, and the rule for both is the one in modules/math/minmax.h, which the tensor
// kernels call too.
slot_t __array_sum__(ArgsView &, ArgsView &norm, Context &) {
    Array *xs = arrayOf(norm);
    switch (codeOf(norm)) {
    case TypeCode::Int64:
        return Reduction<Int64>::sum(xs);
    case TypeCode::Int32:
        // `i32` accumulates in itself, as `+` on `i32` does and as the body's integer arm does.
        return toSlot(sumOf<Int32>(xs));
    case TypeCode::Float64:
        return Reduction<Float64>::sum(xs);
    case TypeCode::Float32:
        return Reduction<Float32>::sum(xs);
    case TypeCode::BFloat16:
        return Reduction<BFloat16>::sum(xs);
    case TypeCode::Float16:
        return Reduction<Float16>::sum(xs);
    default:
        // The declaration's condition refuses these: an element type without an arm runs the body.
        return NullSlot;
    }
}

slot_t __array_min__(ArgsView &, ArgsView &norm, Context &) {
    Array *xs = arrayOf(norm);
    switch (codeOf(norm)) {
    case TypeCode::Int64:
        return Reduction<Int64>::min(xs);
    case TypeCode::Int32:
        return Reduction<Int32>::min(xs);
    case TypeCode::Float64:
        return Reduction<Float64>::min(xs);
    case TypeCode::Float32:
        return Reduction<Float32>::min(xs);
    case TypeCode::BFloat16:
        return Reduction<BFloat16>::min(xs);
    case TypeCode::Float16:
        return Reduction<Float16>::min(xs);
    case TypeCode::Bool:
        // `false` is below `true`, the order `math`'s `min_b`/`max_b` state.
        return Reduction<Bool>::min(xs);
    case TypeCode::String:
        // Byte order, the order `math`'s `min_s`/`max_s` state.
        return minOrMaxOfString(xs, true);
    default:
        // The declaration's condition refuses these, so no element type reaches here.
        return NullSlot;
    }
}

slot_t __array_max__(ArgsView &, ArgsView &norm, Context &) {
    Array *xs = arrayOf(norm);
    switch (codeOf(norm)) {
    case TypeCode::Int64:
        return Reduction<Int64>::max(xs);
    case TypeCode::Int32:
        return Reduction<Int32>::max(xs);
    case TypeCode::Float64:
        return Reduction<Float64>::max(xs);
    case TypeCode::Float32:
        return Reduction<Float32>::max(xs);
    case TypeCode::BFloat16:
        return Reduction<BFloat16>::max(xs);
    case TypeCode::Float16:
        return Reduction<Float16>::max(xs);
    case TypeCode::Bool:
        return Reduction<Bool>::max(xs);
    case TypeCode::String:
        return minOrMaxOfString(xs, false);
    default:
        // As `min`: no element type the declaration's condition admits reaches here.
        return NullSlot;
    }
}
