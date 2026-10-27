// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The native kernels of the `array` module's reductions: `sum`, `min` and `max` over an array of
 * any rank (stdlib/array.cml declares them with `#op`, and each is the executor of the Camel
 * function beside it, which stays the semantics). A reduction sweeps every element in row-major
 * order and accumulates; a scalar binary combines two values -- they are different operations,
 * and the executor of a reduction belongs to the module that declares it (design principles
 * 10.4). The comparison and the summation are written once, in modules/math/minmax.h, which the
 * tensor kernels read too.
 *
 * One executor per element type, and one body-less declaration per element type in
 * stdlib/array.cml: `#op('array:sum_i')` on the declaration whose `when` admits `i32` alone,
 * `#op('array:sum_d')` on the one whose `when` admits `float` alone, and so on for the four
 * element types the reductions are adopted for (`i32`, `int`, `f32`, `float`). The element type is
 * therefore known where the executor is chosen, so this file never asks what an array's element
 * type is. An element type no declaration admits has no executor and keeps the Camel body, which
 * is the semantics (design principles 10.4; one operator, per-type executors).
 *
 * What each kernel computes is what the Camel function beside it says:
 *
 *   - `sum`: an integer element type accumulates in itself and wraps on overflow, as `+` does on
 *     it; a floating-point element type accumulates in f64 and is rounded once into the element
 *     type at the end (design principles 7.3). The sum of no element is 0;
 *   - `min` and `max`: the fold carries an element of T and combines it with
 *     `math::min`/`math::max`, which is IEEE 754-2019 minimum and maximum -- exactly associative
 *     and commutative (design principles 7.7) -- so the order does not change the bits and there
 *     is nothing to round. The result is a `T`; a reduction over no element is a fault
 *     (`InvalidArgument`).
 */

#include "array.h"

#include "camel/core/error/runtime.h"
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
using namespace camel::core::error;
using namespace camel::core::rtdata;
using camel::core::type::ArrayType;

namespace {

/// The array of the first normal argument.
Array *arrayOf(ArgsView &norm) { return norm.get<Array *>(0); }

/// The sum of the elements of an array of any rank, from +0 in row-major order.
template <typename T> T sumOf(Array *xs) {
    const size_t n = xs->numel();
    if constexpr (std::is_floating_point_v<T>) {
        double acc = 0.0;
        for (size_t i = 0; i < n; ++i)
            acc += static_cast<double>(xs->get<T>(i));
        return static_cast<T>(acc);
    } else {
        T acc{};
        for (size_t i = 0; i < n; ++i)
            acc = static_cast<T>(acc + xs->get<T>(i));
        return acc;
    }
}

/// The `min` (minimum = true) or `max` of the elements of an array of any rank. A reduction over
/// no element is a fault (design principles 7.7), as the body of the Camel function says.
template <typename T> slot_t minOrMaxOf(Array *xs, bool minimum) {
    const size_t n = xs->numel();
    if (n == 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            minimum ? "array.min" : "array.max",
            "a reduction over no elements");
    }
    T best = xs->get<T>(0);
    for (size_t i = 1; i < n; ++i) {
        const T x = xs->get<T>(i);
        best      = minimum ? camel::core::math::minOf(best, x) : camel::core::math::maxOf(best, x);
    }
    return toSlot(best);
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

// `min`:

/// `array:min_i`: the least element of an `i32` array of any rank; a fault when it has none.
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

// `max`:

/// `array:max_i`: the greatest element of an `i32` array of any rank; a fault when it has none.
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
