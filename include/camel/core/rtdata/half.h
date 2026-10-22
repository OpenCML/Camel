// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two reduced-precision floating element types of a tensor (design principles 7.3):
 *
 *   bf16  1 sign bit, 8 exponent bits (bias 127), 7 significand bits. It is the top half of an
 *         f32, so every bf16 value is an f32 value exactly: the range of f32 with an 8-bit
 *         significand.
 *   f16   1 sign bit, 5 exponent bits (bias 15), 10 significand bits: IEEE 754-2019 binary16.
 *
 * Both are stored in two bytes and are element types of a tensor only; the language has no
 * bf16 or f16 literal or scalar operation. The conversions are the only operations on the bit
 * patterns:
 *
 *   to the wider type     exact for every stored value, including subnormals, infinities and
 *                         NaNs (a bf16 or f16 NaN is an f32 NaN with the same sign);
 *   from the wider type   round to nearest, ties to even, once. The three paths are the three
 *                         cases of IEEE 754: a normal result, a subnormal result (a value below
 *                         the least normal exponent, rounded to a multiple of 2^-24 for f16),
 *                         and overflow (a value at or above the halfway point between the
 *                         largest finite value and 2^16 becomes an infinity). A NaN stays a NaN;
 *                         its payload is not preserved.
 *
 * The arithmetic of a kernel is computed in f32 on values converted to f32 and rounded once
 * into the element type when it is stored (see modules/tensor/dtype.h); these types define only
 * that store rounding, through their conversion constructor and the arithmetic operators below,
 * which every generic kernel uses.
 */

#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
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
inline uint32_t bfloat16ToFloatBits(uint16_t bits) {
    return static_cast<uint32_t>(bits) << 16;
}

/// The bits of an f16: round to nearest, ties to even, once, through all three IEEE cases.
inline uint16_t float16FromFloat(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
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
        const double scaled = std::ldexp(value64, 24);
        double units         = std::floor(scaled);
        const double fraction = scaled - units;
        if (fraction > 0.5 || (fraction == 0.5 && std::fmod(units, 2.0) != 0.0)) {
            units += 1.0;
        }
        return static_cast<uint16_t>(sign | static_cast<uint16_t>(units));
    }
    // A normal f16: the f32 exponent is in [1, 30] after rebasing, and the 23 significand bits
    // round to 10. Every f32 subnormal is below 2^-14, so the branch above took it and the biased
    // exponent read here is never zero: the subtraction cannot wrap.
    uint32_t exponent = (magnitude >> 23) - 127u + 15u;
    uint32_t significand = magnitude & 0x007FFFFFu;
    uint32_t rounded     = significand >> 13;
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
    const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
    const uint32_t exponent = (bits >> 10) & 0x1Fu;
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

    friend Float16 operator+(Float16 a, Float16 b) {
        return Float16(a.toFloat() + b.toFloat());
    }
    friend Float16 operator-(Float16 a, Float16 b) {
        return Float16(a.toFloat() - b.toFloat());
    }
    friend Float16 operator*(Float16 a, Float16 b) {
        return Float16(a.toFloat() * b.toFloat());
    }
    friend Float16 operator/(Float16 a, Float16 b) {
        return Float16(a.toFloat() / b.toFloat());
    }
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

} // namespace camel::core::rtdata