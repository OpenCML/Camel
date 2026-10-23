// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two reduced-precision floating types of the language (design principles 7.3):
 *
 *   bf16  1 sign bit, 8 exponent bits (bias 127), 7 significand bits. It is the top half of an
 *         f32, so every bf16 value is an f32 value exactly: the range of f32 with an 8-bit
 *         significand.
 *   f16   1 sign bit, 5 exponent bits (bias 15), 10 significand bits: IEEE 754-2019 binary16.
 *
 * Both are floating-point types of the language on the same footing as f32 and float (design
 * principles 7.3, "permissions govern what the compiler changes, not what the program writes"):
 * each has literals, a written conversion (`bf16(x)`, `f16(x)`, `x as bf16`), scalar arithmetic
 * and comparison, and a place in an array. Every scalar operation is computed in f32 on values
 * widened to f32 and rounded once back into the element type, which is the one arithmetic path
 * the tensor kernels use as well (modules/tensor/dtype.h). A slot holds a bf16 or f16 as its
 * two-byte pattern in the low half, zero-extended: the same bytes an element occupies in a
 * compact array, so an element read out of an array and a scalar constant are the same slot.
 *
 * The conversions are the only operations on the bit patterns:
 *
 *   to the wider type     exact for every stored value, including subnormals, infinities and
 *                         NaNs (a bf16 or f16 NaN is an f32 NaN with the same sign);
 *   from the wider type   round to nearest, ties to even, once. The three paths are the three
 *                         cases of IEEE 754: a normal result, a subnormal result (a value below
 *                         the least normal exponent, rounded to a multiple of the format's least
 *                         subnormal), and overflow (a magnitude at or above the halfway point
 *                         between the largest finite value and the next power of two becomes an
 *                         infinity). A NaN stays a NaN; its payload is not preserved.
 *
 * The arithmetic of a kernel is computed in f32 on values converted to f32 and rounded once
 * into the element type when it is stored (see modules/tensor/dtype.h); these types define only
 * that store rounding, through their conversion constructor and the arithmetic operators below,
 * which every generic kernel uses.
 */

#pragma once

#include "camel/core/slot.h"
#include "camel/utils/assert.h"
#include "camel/utils/number_text.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace camel::core::rtdata {

/// The bits of a bf16: the upper 16 bits of its f32 value.
inline uint16_t bfloat16FromFloat(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    // A NaN keeps every bit the format can hold and stays a NaN: the top seven significand bits
    // may all be zero, which would read as an infinity.
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0) {
        return static_cast<uint16_t>((bits >> 16) | 0x0040u);
    }
    // Round to nearest, ties to even, on the 16 dropped bits. The addition cannot carry a
    // finite f32 into the NaN range; it carries a value at or above the largest finite bf16
    // into the infinity pattern, which is the IEEE overflow result.
    const uint32_t rounded = bits + 0x7FFFu + ((bits >> 16) & 1u);
    return static_cast<uint16_t>(rounded >> 16);
}

/// The f32 value of a bf16: the bf16 bits are the top half, so this is exact for every pattern.
inline uint32_t bfloat16ToFloatBits(uint16_t bits) { return static_cast<uint32_t>(bits) << 16; }

/// The bits of an f16: round to nearest, ties to even, once, through all three IEEE cases.
inline uint16_t float16FromFloat(float value) {
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
    // A normal f16: the f32 exponent is in [1, 30] after rebasing, and the 23 significand bits
    // round to 10. Every f32 subnormal is below 2^-14, so the branch above took it and the biased
    // exponent read here is never zero: the subtraction cannot wrap.
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

/// The f32 value of an f16, exact for every pattern (an f16 subnormal is an f32 normal).
inline uint32_t float16ToFloatBits(uint16_t bits) {
    const uint32_t sign        = static_cast<uint32_t>(bits & 0x8000u) << 16;
    const uint32_t exponent    = (bits >> 10) & 0x1Fu;
    const uint32_t significand = bits & 0x03FFu;
    if (exponent == 0) {
        if (significand == 0) {
            return sign;
        }
        const float value = static_cast<float>(std::ldexp(static_cast<double>(significand), -24));
        return std::bit_cast<uint32_t>(value) | sign;
    }
    if (exponent == 31) {
        if (significand == 0) {
            return sign | 0x7F800000u;
        }
        return sign | 0x7F800000u | (significand << 13);
    }
    return sign | ((exponent - 15u + 127u) << 23) | (significand << 13);
}

/**
 * A bf16 value. Arithmetic on two of them is computed in f32 and rounded once into bf16, which
 * is the one arithmetic path of the element type (modules/tensor/dtype.h states it): an
 * operator returns the rounded sum/product, and a kernel stores it without another rounding.
 */
struct BFloat16 {
    uint16_t bits = 0;

    BFloat16() = default;
    /// The store rounding. One constructor for every arithmetic source type, so that a kernel
    /// converting a buffer of any element type (an int64 or a bool included) names one
    /// conversion instead of an ambiguous overload set. A double is brought to f32 first: f32 is
    /// the compute precision of every kernel (`modules/tensor/dtype.h`).
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    explicit BFloat16(T value) : bits(bfloat16FromFloat(static_cast<float>(value))) {}

    static BFloat16 fromBits(uint16_t raw) {
        BFloat16 value;
        value.bits = raw;
        return value;
    }
    float toFloat() const { return std::bit_cast<float>(bfloat16ToFloatBits(bits)); }
    /// The value as any arithmetic type. A conversion function template, so the target type is
    /// deduced and the conversion stays explicit: `static_cast<int64_t>` and `static_cast<double>`
    /// are each one conversion, with no second one to make the overload set ambiguous, and no
    /// implicit conversion exists in either direction (design principles 5.1).
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    explicit operator T() const {
        return static_cast<T>(toFloat());
    }

    friend BFloat16 operator+(BFloat16 a, BFloat16 b) {
        return BFloat16(a.toFloat() + b.toFloat());
    }
    friend BFloat16 operator-(BFloat16 a, BFloat16 b) {
        return BFloat16(a.toFloat() - b.toFloat());
    }
    friend BFloat16 operator*(BFloat16 a, BFloat16 b) {
        return BFloat16(a.toFloat() * b.toFloat());
    }
    friend BFloat16 operator/(BFloat16 a, BFloat16 b) {
        return BFloat16(a.toFloat() / b.toFloat());
    }
    friend BFloat16 operator-(BFloat16 a) { return BFloat16(-a.toFloat()); }
    friend bool operator==(BFloat16 a, BFloat16 b) { return a.toFloat() == b.toFloat(); }
    friend bool operator!=(BFloat16 a, BFloat16 b) { return a.toFloat() != b.toFloat(); }
    friend bool operator<(BFloat16 a, BFloat16 b) { return a.toFloat() < b.toFloat(); }
    friend bool operator<=(BFloat16 a, BFloat16 b) { return a.toFloat() <= b.toFloat(); }
    friend bool operator>(BFloat16 a, BFloat16 b) { return a.toFloat() > b.toFloat(); }
    friend bool operator>=(BFloat16 a, BFloat16 b) { return a.toFloat() >= b.toFloat(); }
};

/// An f16 value, with the same one arithmetic path as BFloat16.
struct Float16 {
    uint16_t bits = 0;

    Float16() = default;
    /// The store rounding, with the same one constructor as BFloat16.
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    explicit Float16(T value) : bits(float16FromFloat(static_cast<float>(value))) {}

    static Float16 fromBits(uint16_t raw) {
        Float16 value;
        value.bits = raw;
        return value;
    }
    float toFloat() const { return std::bit_cast<float>(float16ToFloatBits(bits)); }
    /// The value as any arithmetic type, as in BFloat16.
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    explicit operator T() const {
        return static_cast<T>(toFloat());
    }

    friend Float16 operator+(Float16 a, Float16 b) { return Float16(a.toFloat() + b.toFloat()); }
    friend Float16 operator-(Float16 a, Float16 b) { return Float16(a.toFloat() - b.toFloat()); }
    friend Float16 operator*(Float16 a, Float16 b) { return Float16(a.toFloat() * b.toFloat()); }
    friend Float16 operator/(Float16 a, Float16 b) { return Float16(a.toFloat() / b.toFloat()); }
    friend Float16 operator-(Float16 a) { return Float16(-a.toFloat()); }
    friend bool operator==(Float16 a, Float16 b) { return a.toFloat() == b.toFloat(); }
    friend bool operator!=(Float16 a, Float16 b) { return a.toFloat() != b.toFloat(); }
    friend bool operator<(Float16 a, Float16 b) { return a.toFloat() < b.toFloat(); }
    friend bool operator<=(Float16 a, Float16 b) { return a.toFloat() <= b.toFloat(); }
    friend bool operator>(Float16 a, Float16 b) { return a.toFloat() > b.toFloat(); }
    friend bool operator>=(Float16 a, Float16 b) { return a.toFloat() >= b.toFloat(); }
};

/// True for the two reduced-precision floating types (H238).
template <typename T> struct IsReducedFloat : std::false_type {};
template <> struct IsReducedFloat<BFloat16> : std::true_type {};
template <> struct IsReducedFloat<Float16> : std::true_type {};
template <typename T> inline constexpr bool is_reduced_float_v = IsReducedFloat<T>::value;

/// True for the types an IEEE 754-2019 floating operation is defined on here: f32, f64, bf16,
/// f16. A kernel asks this where it must accumulate in a wider type and order NaNs and zeros.
template <typename T>
inline constexpr bool is_ieee_float_v = std::is_floating_point_v<T> || is_reduced_float_v<T>;

/*
 * The slot form of a bf16 or an f16 (the two bytes of the format in the low half, zero-extended)
 * and the conversions between that form and the types a slot can hold. One implementation is
 * shared by the language's written conversions (`bf16(x)`, `f16(x)`, `x as bf16`), by a scalar
 * constant of either type (core/data), and by the scalar arithmetic of every back end, so a
 * conversion rounds the same way wherever it is met.
 */

/// The 16-bit pattern a bf16 or an f16 slot holds.
inline uint16_t halfBitsOfSlot(slot_t slot) {
    uint16_t bits = 0;
    std::memcpy(&bits, &slot, sizeof(bits));
    return bits;
}

/// The slot of a 16-bit pattern (the type is the caller's: bf16 or f16).
inline slot_t halfSlotOfBits(uint16_t bits) {
    slot_t slot{};
    std::memcpy(&slot, &bits, sizeof(bits));
    return slot;
}

/// A bf16 or f16 as the f32 it widens to, exactly, for every pattern.
template <typename Half> inline float halfToFloat(slot_t slot) {
    const uint16_t bits = halfBitsOfSlot(slot);
    if constexpr (std::is_same_v<Half, BFloat16>) {
        return std::bit_cast<float>(bfloat16ToFloatBits(bits));
    } else {
        return std::bit_cast<float>(float16ToFloatBits(bits));
    }
}

/// The nearest bf16 or f16 of an f32, round to nearest, ties to even, once.
template <typename Half> inline slot_t halfFromFloat(float value) {
    if constexpr (std::is_same_v<Half, BFloat16>) {
        return halfSlotOfBits(bfloat16FromFloat(value));
    } else {
        return halfSlotOfBits(float16FromFloat(value));
    }
}

/// The same store rounding from a wider value of any arithmetic type: the value is brought to
/// f32 first, which is the compute precision of the element type, and rounded once into it.
template <typename Half, typename T> inline slot_t halfFromValue(const T &value) {
    return halfFromFloat<Half>(static_cast<float>(value));
}

/// Whether a written value is beyond what one of the two types can hold: a finite value whose
/// nearest bf16 or f16 is an infinity. The boundary is the halfway point between the largest
/// finite value and the next power of two (2^128 for bf16, 2^16 for f16), which is where IEEE
/// rounding overflows; a written value at or above it is the overflow a written conversion
/// reports, the same way an f32 conversion reports a value beyond the range of f32. A value that
/// rounds to zero is not an overflow: zero is a value of the type (H15i calls it an underflow).
template <typename Half> inline bool halfOverflows(float value) {
    if (!std::isfinite(value)) {
        return false;
    }
    return std::isinf(halfToFloat<Half>(halfFromFloat<Half>(value)));
}

/*
 * The text of a bf16 or an f16, and the one rule it follows. Design principles 7.7 says a float
 * becomes text as the shortest text that reads back as the same value; the ruling of 2026-10-23
 * ("reduced-precision-text") says "the same value" is an element of the value's own type, so a
 * bf16 or an f16 is measured against its own rounding: `0.1 as bf16` prints `0.1`, where the text
 * of the f32 it widens to would print `0.100097656`. The digits are found by
 * camel::utils::halfText -- the one float text rule, parameterised by the format -- and every
 * path that turns one of these values into text goes through `halfText` below.
 */

/// The text of a bf16 or an f16: the shortest digits that read back as the same element of its
/// own type. A candidate reads back when parsing it and applying the type's store rounding gives
/// the same pattern: the widened f32 of the value is exact, and every pattern rounds to itself,
/// so the text a value prints always reads back as that value. This overload is the one
/// `floatText` of a half value resolves to; the generic camel::utils::floatText never takes these
/// types, because its digits come from std::to_chars, which has no overload for them.
template <typename Half> std::string halfText(Half value) {
    return camel::utils::halfText(value, [value](std::string_view scientific) {
        float parsed = 0.0f;
        if (!camel::utils::scientificParsesBack(scientific, parsed)) {
            return false;
        }
        return halfFromFloat<Half>(parsed) == halfSlotOfBits(value.bits);
    });
}

/// `floatText` of a bf16 or an f16, as the language's own name for that text (the printer and the
/// constant text both write it through `halfText`; this overload exists so a caller that holds a
/// value of the type can write `camel::core::rtdata::floatText(value)` and get the type's own
/// rule rather than a silently widened one). `IsHalfFloat` is specialized below, so
/// camel::utils::floatText refuses these values and the two names never collide.
template <typename Half>
    requires IsReducedFloat<Half>::value
std::string floatText(Half value) {
    return halfText(value);
}

} // namespace camel::core::rtdata

namespace camel::utils {

/// Marks the two types so that camel::utils::floatText refuses them: a bf16 or an f16 never takes
/// the f32 rule. Its text is camel::core::rtdata::halfText above, which the printer, the constant
/// text and the `format` path all write; the specializations must be here, in the namespace the
/// template is declared in.
template <> struct IsHalfFloat<camel::core::rtdata::BFloat16> : std::true_type {};
template <> struct IsHalfFloat<camel::core::rtdata::Float16> : std::true_type {};

} // namespace camel::utils