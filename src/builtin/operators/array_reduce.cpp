// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The native kernels of the `array` module's reductions: `sum`, `min` and `max` over an array
 * (stdlib/array.cml declares them with `#op`, and each is the executor of the Camel function
 * beside it, which stays the semantics).
 *
 * A reduction sweeps the array and accumulates; a scalar binary combines two values. They
 * are different operations, and the executor of a reduction belongs to the module that declares it
 * (design principles 10.4). An `array` kernel does not name
 * `math`'s operator, and `sum` is not left without a kernel.
 *
 * The comparison and the summation are written once: the `array:*` executors call the templates
 * already in `modules/math/minmax.h`, so
 * "how to compare two bf16s" is stated in one place in C++ and the tensor kernels read the same
 * header.
 *
 * One executor per element type, and one body-less declaration per element type in
 * stdlib/array.cml: `#op<'array:sum_i'>` on the declaration whose `when` admits `i32` alone,
 * `#op<'array:sum_d'>` on the one whose `when` admits `float` alone, and so on for every element
 * type the module's reductions are declared for (six numeric types for `sum`; those and `bool` and
 * `string` for `min` and `max`). The element type is therefore known where the executor is chosen:
 * the compiler asks each declaration's own `when` about the instance's element type
 * (`Builder::declaredRuleApplies`) and adopts the rule whose condition holds, so this file never
 * asks what an array's element type is. There is no `switch (elemTypeCode)` here and no default arm
 * to guess with: an element type no declaration's condition admits has no executor at all and keeps
 * the Camel body, which is the semantics (design principles 10.4: one operator, per-type
 * executors). A `c64[]` sum is the case: no kernel declaration admits a complex element type, so
 * `sum` over it runs the body, whose complex arm is the only place that says what summing complex
 * numbers means.
 *
 * What each kernel computes is what the Camel function beside it says (the body is the
 * semantics):
 *
 *   - `sum`: an integer element type accumulates in itself and wraps on overflow, as `+` does on
 *     it (i32 sums in i32, int sums in int); a floating-point element type accumulates in f64 and
 *     is rounded once into the element type at the end (design principles 7.3), which is what
 *     stdlib/array.cml's `sumF64` plus its one conversion spell out. bf16 and f16 are
 *     floating-point element types of that same kind and have their own converters;
 *   - `min` and `max`: no accumulator and no accumulation width take part. The fold carries an
 *     element of T and combines it with `math::min`/`math::max`, which is IEEE 754-2019 minimum
 *     and maximum — exactly associative and commutative (design principles 7.7) — so the order
 *     does not change the bits and there is nothing to round. The result is `T?`: null for an
 *     empty array. A bf16 or f16 fold carries the winning element's own pattern, and a tie between
 *     two equal values is the sign of a zero, decided by the rule the header states rather than by
 *     the slot the array happens to hold first.
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

} // namespace

// The executors, one per element type. Each is the body of one body-less declaration of
// stdlib/array.cml, named by that declaration's `#op` tag, and each takes exactly one element type
// — the one whose condition admits it — so nothing here reads the array's element type.
//
// `sum`:

/// `array:sum_i`: the sum of an `i32[]`, accumulated in `i32` itself, wrapping on overflow as `+`
/// on `i32` does.
slot_t __array_sum_i__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(sumOf<Int32>(arrayOf(norm)));
}

/// `array:sum_l`: the same for an `int[]` (i64).
slot_t __array_sum_l__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(sumOf<Int64>(arrayOf(norm)));
}

/// `array:sum_f`: the sum of an `f32[]`, accumulated in f64 from +0 and rounded once into f32.
slot_t __array_sum_f__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(sumOf<Float32>(arrayOf(norm)));
}

/// `array:sum_d`: the same for a `float[]` (f64); the accumulator is the element type itself.
slot_t __array_sum_d__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(sumOf<Float64>(arrayOf(norm)));
}

/// `array:sum_bf`: the sum of a `bf16[]`, accumulated in f64 and converted once, without the
/// f32-mediated conversion of half.h.
slot_t __array_sum_bf__(ArgsView &, ArgsView &norm, Context &) {
    return sumOfHalf<BFloat16>(arrayOf(norm));
}

/// `array:sum_f6`: the same for an `f16[]`.
slot_t __array_sum_f6__(ArgsView &, ArgsView &norm, Context &) {
    return sumOfHalf<Float16>(arrayOf(norm));
}

// `min`:

/// `array:min_i`: the least element of an `i32[]`, or null when it is empty.
slot_t __array_min_i__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Int32>(arrayOf(norm), true);
}

/// `array:min_l`: the same for an `int[]`.
slot_t __array_min_l__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Int64>(arrayOf(norm), true);
}

/// `array:min_f`: the least element of an `f32[]` (IEEE 754 minimum).
slot_t __array_min_f__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Float32>(arrayOf(norm), true);
}

/// `array:min_d`: the same for a `float[]`.
slot_t __array_min_d__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Float64>(arrayOf(norm), true);
}

/// `array:min_bf`: the least element of a `bf16[]`, carried as that element's own pattern.
slot_t __array_min_bf__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOfHalf<BFloat16>(arrayOf(norm), true);
}

/// `array:min_f6`: the same for an `f16[]`.
slot_t __array_min_f6__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOfHalf<Float16>(arrayOf(norm), true);
}

/// `array:min_b`: the least element of a `bool[]`: `false` below `true`.
slot_t __array_min_b__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Bool>(arrayOf(norm), true);
}

/// `array:min_s`: the least element of a `string[]`, by the bytes of the two values.
slot_t __array_min_s__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOfString(arrayOf(norm), true);
}

// `max`:

/// `array:max_i`: the greatest element of an `i32[]`, or null when it is empty.
slot_t __array_max_i__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Int32>(arrayOf(norm), false);
}

/// `array:max_l`: the same for an `int[]`.
slot_t __array_max_l__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Int64>(arrayOf(norm), false);
}

/// `array:max_f`: the greatest element of an `f32[]` (IEEE 754 maximum).
slot_t __array_max_f__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Float32>(arrayOf(norm), false);
}

/// `array:max_d`: the same for a `float[]`.
slot_t __array_max_d__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Float64>(arrayOf(norm), false);
}

/// `array:max_bf`: the greatest element of a `bf16[]`, carried as that element's own pattern.
slot_t __array_max_bf__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOfHalf<BFloat16>(arrayOf(norm), false);
}

/// `array:max_f6`: the same for an `f16[]`.
slot_t __array_max_f6__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOfHalf<Float16>(arrayOf(norm), false);
}

/// `array:max_b`: the greatest element of a `bool[]`: `false` below `true`.
slot_t __array_max_b__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOf<Bool>(arrayOf(norm), false);
}

/// `array:max_s`: the greatest element of a `string[]`, by the bytes of the two values.
slot_t __array_max_s__(ArgsView &, ArgsView &norm, Context &) {
    return minOrMaxOfString(arrayOf(norm), false);
}