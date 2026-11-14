// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * The two IEEE 754-2019 comparisons the language's `min` and `max` are, for every type they are
 * defined on (design principles 7.7): a NaN on either side gives the canonical quiet NaN, and -0
 * is below +0 — equal operands are identical except for the sign of zero, which decides between
 * them.
 *
 * This is the one statement of that comparison in C++, and every consumer calls it:
 *
 *   - `numeric.cpp` for f32 and f64, `operators.cpp` for bf16 and f16,
 *     which is why the reduced-precision overloads are the same operation on the value each
 *     operand widens to rather than a second copy that could drift;
 *   - the `array` module's `sum`, `min` and `max` kernels (builtin/operators/array_reduce.cpp):
 *     a reduction along an array, which is a different operation from a scalar binary but combines
 *     its elements with the same comparison, so it holds the same header;
 *   - the tensor module's kernels (`modules/tensor/kernels/minmax.h`), which forward their
 *     `maximum`/`minimum` to `maxOf`/`minOf` here.
 *
 * A bf16 or an f16 is compared as the f32 it widens to, exactly (camel/core/rtdata/half.h), and
 * the result is one of the two operands converted back: the conversion is exact for a value the
 * element type holds, so nothing is rounded and no accumulator takes part.
 */

#include "camel/core/rtdata/half.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace camel::core::math {

namespace detail {
/// The unsigned integer type of the bits of a float type.
template <typename T> using FloatBits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;

/// The comparison is made on the values widened to f32, so a bf16 or an f16 needs no path of its
/// own: this is the type whose bits carry the comparison for `T`.
template <typename T> using CompareType = std::conditional_t<rtdata::is_reduced_float_v<T>, float, T>;
template <typename T> using CompareBits = FloatBits<CompareType<T>>;

/// The value as the comparison sees it: an f16 or a bf16 is the f32 it widens to, exactly.
template <typename T> constexpr CompareType<T> widened(T value) {
    if constexpr (rtdata::is_reduced_float_v<T>) {
        return value.toFloat();
    } else {
        return value;
    }
}

/// The same value back in `T`. For an f32 or an f64 this is the value itself; for a
/// reduced-precision type it is the narrowing every value of the comparison came from, and the
/// inputs to `min`/`max` are values the type holds, so the bits are the ones that went in.
template <typename T> constexpr T narrowed(CompareType<T> value) {
    if constexpr (rtdata::is_reduced_float_v<T>) {
        return T(value);
    } else {
        return value;
    }
}

/// True when `T` is one the NaN and signed-zero rules are stated for: an IEEE 754 float, in the
/// element type's own width or one of the two reduced-precision types.
template <typename T> inline constexpr bool isIeeeFloat = rtdata::is_ieee_float_v<T>;

/// One comparison, IEEE 754-2019 (see the file comment), returning one of its two operands. With
/// `maximum` set it is maximum, otherwise minimum.
///
/// The operand is chosen on the values, but returned as its own bits: a value derived from the
/// comparison would have to be converted back, and that conversion is where the sign of a zero is
/// lost and a rounded result moves. `+-0` is the case that needs it — the two compare equal, and
/// the sign bit is what decides — so the two sign bits are combined and only a mixed pair lets
/// the comparison choose between them. For a reduced-precision type the widened comparison is an
/// f32 one, which only ever sees the operand's own exact f32 image, so narrowing either operand
/// gives its own 16-bit pattern back.
template <typename T, bool maximum> constexpr T compareOf(T a, T b) {
    using C    = CompareType<T>;
    using Bits = CompareBits<T>;
    const C x  = widened(a);
    const C y  = widened(b);
    if (x != x || y != y) {
        // The canonical quiet NaN of the type whose values are compared; for a reduced-precision
        // type that is the f32 one, rounded into the type (its constructor is explicit).
        const C quiet = std::numeric_limits<C>::quiet_NaN();
        if constexpr (rtdata::is_reduced_float_v<T>) {
            return T(quiet);
        } else {
            return quiet;
        }
    }
    if (x != y) {
        return (maximum ? x < y : y < x) ? b : a;
    }
    // Equal values: identical but for the sign of zero. The AND keeps a sign bit only when both
    // operands are -0 (so a mixed pair gives +0 for both operations), and the OR keeps it when
    // either is -0 (so it gives -0 for both).
    const Bits bitsA = std::bit_cast<Bits>(x);
    const Bits bitsB = std::bit_cast<Bits>(y);
    const Bits bits  = maximum ? (bitsA & bitsB) : (bitsA | bitsB);
    return narrowed<T>(std::bit_cast<C>(bits));
}
} // namespace detail

/// IEEE 754-2019 maximum of two values of the same type (see the file comment).
template <typename T> T maxOf(T a, T b) {
    if constexpr (detail::isIeeeFloat<T>) {
        return detail::compareOf<T, true>(a, b);
    } else {
        return a < b ? b : a;
    }
}

/// IEEE 754-2019 minimum of two values of the same type (see the file comment).
template <typename T> T minOf(T a, T b) {
    if constexpr (detail::isIeeeFloat<T>) {
        return detail::compareOf<T, false>(a, b);
    } else {
        return b < a ? b : a;
    }
}

} // namespace camel::core::math