// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * bf16 and f16 in the programs std::cpp writes: the two reduced-precision floating types of the
 * language (design principles 7.3), which are numbers on the same footing as f32 and float.
 *
 *   bf16  1 sign bit, 8 exponent bits, 7 significand bits: the top half of an f32, so every bf16
 *         is an f32 value exactly.
 *   f16   1 sign bit, 5 exponent bits, 10 significand bits: IEEE 754-2019 binary16.
 *
 * A value of either is 16 bits of storage and is held here as its pattern; the arithmetic of the
 * language is done in f32 on the widened operands and the result is rounded once into the element
 * type, which is the one arithmetic path the runtime's kernels use as well
 * (camel/core/rtdata/half.h, modules/tensor/dtype.h). Every conversion into the type is that store
 * rounding, so a C++ program holds the pattern std::fvm holds.
 *
 * This header carries its own bit-level rounding because the standalone runtime is standard-only
 * (camel/standalone/core.h): it cannot include camel/core/rtdata/half.h, which is built on the
 * runtime's slot. The rounding rule is the same one, down to the three IEEE cases of a narrowing
 * conversion: a normal result, a subnormal result, and overflow to an infinity, with NaN kept a
 * NaN and its payload not preserved. The text rule is shared rather than copied: `halfText` below
 * asks camel::utils::halfText for the shortest digits, with "reads back" measured against this
 * type's store rounding.
 */

#pragma once

#include "camel/utils/number_text.h"

#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace camel::standalone {

/// The facts of a reduced-precision format: the name the language writes it with.
struct BF16Format {
    static constexpr const char *name = "bf16";
};
struct F16Format {
    static constexpr const char *name = "f16";
};

// ---- The store rounding, per format ----

/// The bits of `value` as a bf16: round to nearest, ties to even, on the 16 dropped bits.
template <typename Format> inline uint16_t halfBitsFromFloat(float value);

template <> inline uint16_t halfBitsFromFloat<BF16Format>(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    // A NaN keeps a non-zero significand: the top seven bits may all be zero, which would read as
    // an infinity.
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0) {
        return static_cast<uint16_t>((bits >> 16) | 0x0040u);
    }
    // The addition carries a value at or above the largest finite bf16 into the infinity pattern,
    // which is the IEEE overflow result.
    const uint32_t rounded = bits + 0x7FFFu + ((bits >> 16) & 1u);
    return static_cast<uint16_t>(rounded >> 16);
}

/// The bits of `value` as an f16: round to nearest, ties to even, once, through all three cases.
template <> inline uint16_t halfBitsFromFloat<F16Format>(float value) {
    const uint32_t bits      = std::bit_cast<uint32_t>(value);
    const uint16_t sign      = static_cast<uint16_t>((bits >> 16) & 0x8000u);
    const uint32_t magnitude = bits & 0x7FFFFFFFu;
    if (magnitude > 0x7F800000u) { // NaN
        uint16_t payload = static_cast<uint16_t>((magnitude >> 13) & 0x03FFu);
        if (payload == 0) {
            payload = 1; // an f16 NaN needs a non-zero significand
        }
        return static_cast<uint16_t>(sign | 0x7C00u | payload);
    }
    if (magnitude == 0x7F800000u) { // infinity
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    const double value64 = std::fabs(static_cast<double>(std::bit_cast<float>(magnitude)));
    // The halfway point between the largest finite f16 (65504) and 2^16: ties to even give the
    // even neighbour, 2^16, which is not an f16, so the result is an infinity.
    if (value64 >= 65520.0) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (value64 < 6.103515625e-05) { // below the least normal f16, 2^-14
        // The value in units of 2^-24, rounded to nearest with ties to even. A double holds the
        // f32 and the scaled value exactly, so this is the IEEE rounding, not an approximation.
        const double scaled   = std::ldexp(value64, 24);
        double units          = std::floor(scaled);
        const double fraction = scaled - units;
        if (fraction > 0.5 || (fraction == 0.5 && std::fmod(units, 2.0) != 0.0)) {
            units += 1.0;
        }
        return static_cast<uint16_t>(sign | static_cast<uint16_t>(units));
    }
    // A normal f16: every f32 subnormal is below 2^-14 and the branch above took it, so the biased
    // exponent read here is never zero and the rebasing cannot wrap.
    uint32_t exponent        = (magnitude >> 23) - 127u + 15u;
    uint32_t significand     = magnitude & 0x007FFFFFu;
    uint32_t rounded         = significand >> 13;
    const uint32_t remainder = significand & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (rounded & 1u) != 0)) {
        ++rounded;
        if (rounded == 0x0400u) { // the significand carried into the exponent
            rounded = 0;
            ++exponent;
        }
    }
    if (exponent >= 31u) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | (exponent << 10) | rounded);
}

/// The f32 value of the pattern `bits`, exact for every pattern (a bf16 is the top half of an f32
/// and an f16 subnormal is an f32 normal).
template <typename Format> inline float halfFloatFromBits(uint16_t bits);

template <> inline float halfFloatFromBits<BF16Format>(uint16_t bits) {
    return std::bit_cast<float>(static_cast<uint32_t>(bits) << 16);
}

template <> inline float halfFloatFromBits<F16Format>(uint16_t bits) {
    const uint32_t sign        = static_cast<uint32_t>(bits & 0x8000u) << 16;
    const uint32_t exponent    = (bits >> 10) & 0x1Fu;
    const uint32_t significand = bits & 0x03FFu;
    if (exponent == 0) {
        if (significand == 0) {
            return std::bit_cast<float>(sign);
        }
        const float value = static_cast<float>(std::ldexp(static_cast<double>(significand), -24));
        return std::bit_cast<float>(std::bit_cast<uint32_t>(value) | sign);
    }
    if (exponent == 31) {
        if (significand == 0) {
            return std::bit_cast<float>(sign | 0x7F800000u);
        }
        return std::bit_cast<float>(sign | 0x7F800000u | (significand << 13));
    }
    return std::bit_cast<float>(sign | ((exponent - 15u + 127u) << 23) | (significand << 13));
}

/**
 * A bf16 or an f16 value (rt::BF16, rt::F16 below): sixteen bits of storage holding the pattern of
 * the type (design principles 7.3), the widening to f32 that is exact, and the store rounding every
 * conversion into it goes through.
 *
 * Arithmetic on two of them is computed in f32 and rounded once into the element type, which is the
 * one arithmetic path of the element type: an operator returns the rounded sum, difference, product
 * or negation. Division is `rt::div`, because dividing by zero is a fault and an operator cannot
 * report one (num.h). Comparison is the comparison of the widened values, so a NaN equals nothing
 * and every order against it is false.
 */
template <typename Format> struct Half {
    uint16_t bits = 0;

    /// The format this value type is made of (BF16Format or F16Format): a template that takes the
    /// value type where it needs the format -- `halfFromText<rt::BF16>` in num.h -- asks for it
    /// through this name, so the two back ends can spell the same conversion.
    using FormatOf = Format;

    Half() = default;
    /// The store rounding. One constructor for every arithmetic source type, so that a kernel
    /// converting a buffer of any element type names one conversion; a wider value is brought to
    /// f32 first, which is the compute precision of the element type.
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    explicit Half(T value) : bits(halfBitsFromFloat<Format>(static_cast<float>(value))) {}

    static Half fromBits(uint16_t raw) {
        Half value;
        value.bits = raw;
        return value;
    }
    float toFloat() const { return halfFloatFromBits<Format>(bits); }
    /// The value as any arithmetic type: a conversion function template, so the target type is
    /// deduced and the conversion stays explicit (`static_cast<float>`, `static_cast<int64_t>`).
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    explicit operator T() const {
        return static_cast<T>(toFloat());
    }

    friend Half operator+(Half a, Half b) { return Half(a.toFloat() + b.toFloat()); }
    friend Half operator-(Half a, Half b) { return Half(a.toFloat() - b.toFloat()); }
    friend Half operator*(Half a, Half b) { return Half(a.toFloat() * b.toFloat()); }
    friend Half operator-(Half a) { return Half(-a.toFloat()); }
    /// Equality and order of the widened values; the partial order keeps a NaN unordered, so `<`,
    /// `<=`, `>` and `>=` are false of it exactly as the interpreter's comparison is.
    friend bool operator==(Half a, Half b) { return a.toFloat() == b.toFloat(); }
    friend std::partial_ordering operator<=>(Half a, Half b) { return a.toFloat() <=> b.toFloat(); }
};

/// The two types the language writes.
using BF16 = Half<BF16Format>;
using F16  = Half<F16Format>;

/// Whether a written value is beyond what the type can hold: a finite value whose nearest element
/// is an infinity. The boundary is the halfway point between the largest finite value and the next
/// power of two, which is where IEEE rounding overflows; a value that rounds to zero is not an
/// overflow, because zero is a value of the type.
template <typename Format> inline bool halfOverflows(float value) {
    if (!std::isfinite(value)) {
        return false;
    }
    return std::isinf(halfFloatFromBits<Format>(halfBitsFromFloat<Format>(value)));
}

/// The text of a value: the shortest digits that read back as the same element of its own type
/// (design principles 7.7), so `0.1 as bf16` prints `0.1` where the f32 text of its bits prints
/// `0.100097656`. The digits come from camel::utils::halfText, the one float text rule; a
/// candidate reads back when parsing it and applying this type's store rounding gives the same
/// pattern.
template <typename Format> std::string halfText(const Half<Format> &value) {
    return camel::utils::halfText(value, [&value](std::string_view scientific) {
        float parsed = 0.0f;
        if (!camel::utils::scientificParsesBack(scientific, parsed)) {
            return false;
        }
        return halfBitsFromFloat<Format>(parsed) == value.bits;
    });
}

/// One comparison of two values of the same type, IEEE 754-2019 (modules/math/minmax.h, the one
/// statement of it the runtime has): a NaN on either side gives the canonical quiet NaN of the
/// type, `-0` is below `+0`, and otherwise the answer is one of the two operands.
///
/// The answer is an operand's own pattern, never a value computed from the comparison: both
/// operands are values the type holds, so nothing here rounds, and combining the two sign bits is
/// what decides between two zeros that compare equal (`minimum` keeps a sign either operand has,
/// `maximum` keeps one only when both have it).
template <typename Format, bool maximum>
Half<Format> halfCompareOf(const Half<Format> &a, const Half<Format> &b) {
    const float x = a.toFloat();
    const float y = b.toFloat();
    if (x != x || y != y) {
        return Half<Format>(std::numeric_limits<float>::quiet_NaN());
    }
    if (x != y) {
        return (maximum ? x < y : y < x) ? b : a;
    }
    return Half<Format>::fromBits(maximum ? uint16_t(a.bits & b.bits) : uint16_t(a.bits | b.bits));
}

/// `math::min` of two values (:op/math:min_bf, math:min_f6; modules/math/operators.cpp).
template <typename Format> Half<Format> halfMin(const Half<Format> &a, const Half<Format> &b) {
    return halfCompareOf<Format, false>(a, b);
}

/// `math::max` of two values (math:max_bf, math:max_f6).
template <typename Format> Half<Format> halfMax(const Half<Format> &a, const Half<Format> &b) {
    return halfCompareOf<Format, true>(a, b);
}

/// `math::abs` of a value (math:abs_bf, math:abs_f6): the sign bit cleared, which is exact and the
/// only one of the three whose answer differs from an operand's own pattern (`-0` becomes `+0`).
template <typename Format> Half<Format> halfAbs(const Half<Format> &value) {
    return Half<Format>::fromBits(uint16_t(value.bits & 0x7FFFu));
}

} // namespace camel::standalone