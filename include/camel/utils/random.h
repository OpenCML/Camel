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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Random distributions that give the same numbers on every platform.
 *
 * A program's output is the same bytes on every platform (docs/design-principles.cn.md 2.11),
 * and a seeded program prints numbers drawn from a generator. The engine std::mt19937_64 is
 * specified bit for bit by the C++ standard, but the distributions are not: libc++ draws
 * std::uniform_int_distribution and std::normal_distribution differently from libstdc++, so the
 * same seed gave other numbers on macOS. The modules that draw numbers (random, sim, tensor) use
 * the functions here on a std::mt19937_64 instead.
 *
 * The algorithms are the ones libstdc++ uses, so the numbers drawn on Linux before this file
 * existed are kept, except where the logarithm below rounds differently from glibc's (normal
 * draws only: about 3 in 100 differ in the last bit):
 *   unit interval   the 64 engine bits scaled by 2^-64 (std::generate_canonical with one draw),
 *                   kept below 1;
 *   integers        Lemire's nearly divisionless method ("Fast random integer generation in an
 *                   interval", 2019), one 64 x 64 -> 128-bit product per draw;
 *   normal          Marsaglia's polar method; one accepted pair gives two values, the second is
 *                   kept for the next draw of the same NormalSource;
 *   logarithm       fdlibm's e_log.c, in plain double arithmetic, instead of the platform's
 *                   std::log, whose last bit differs between libraries.
 * Every expression is evaluated as written: floating-point contraction (a * b + c as one fused
 * operation, which arm64 compilers do by default) is switched off in each function.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>

namespace camel::utils::random {

using Engine = std::mt19937_64;

namespace detail {

/// The high and low 64 bits of a * b.
inline void mulWide(uint64_t a, uint64_t b, uint64_t &hi, uint64_t &lo) {
    const uint64_t a0 = a & 0xffffffffu, a1 = a >> 32;
    const uint64_t b0 = b & 0xffffffffu, b1 = b >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    lo                 = (mid << 32) | (p00 & 0xffffffffu);
    hi                 = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

} // namespace detail

/// Natural logarithm of a positive finite x, computed the same way on every platform (fdlibm's
/// e_log.c: x = 2^k * (1 + f) with 1 + f in [sqrt(2)/2, sqrt(2)), log(1 + f) from a minimax
/// polynomial in s = f / (2 + f)). Error below 1 ulp.
inline double log(double x) {
#pragma clang fp contract(off)
    constexpr double ln2Hi     = 6.93147180369123816490e-01; // 0x3FE62E42 FEE00000
    constexpr double ln2Lo     = 1.90821492927058770002e-10; // 0x3DEA39EF 35793C76
    constexpr double Lg1       = 6.666666666666735130e-01;   // 0x3FE55555 55555593
    constexpr double Lg2       = 3.999999999940941908e-01;   // 0x3FD99999 9997FA04
    constexpr double Lg3       = 2.857142874366239149e-01;   // 0x3FD24924 94229359
    constexpr double Lg4       = 2.222219843214978396e-01;   // 0x3FCC71C5 1D8E78AF
    constexpr double Lg5       = 1.818357216161805012e-01;   // 0x3FC74664 96CB03DE
    constexpr double Lg6       = 1.531383769920937332e-01;   // 0x3FC39A09 D078C69F
    constexpr double Lg7       = 1.479819860511658591e-01;   // 0x3FC2F112 DF3E5244
    constexpr double kSqrtHalf = 0.70710678118654752440;

    int e    = 0;
    double m = std::frexp(x, &e); // x = m * 2^e, m in [0.5, 1), exact
    if (m < kSqrtHalf) {
        m *= 2.0;
        e -= 1;
    }
    const double f  = m - 1.0; // exact (Sterbenz)
    const double k  = static_cast<double>(e);
    const double s  = f / (2.0 + f);
    const double z  = s * s;
    const double w  = z * z;
    const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    const double r  = t2 + t1;
    // As fdlibm, by the high 20 bits of the significand: for the largest |f| (1 + f below 0.71
    // or above 1.38) 0.5 * f * f is kept apart.
    uint64_t mBits = 0;
    std::memcpy(&mBits, &m, sizeof(m));
    const int32_t hx = static_cast<int32_t>((mBits >> 32) & 0x000fffff);
    if (((hx - 0x6147a) | (0x6b851 - hx)) > 0) {
        const double hfsq = 0.5 * f * f;
        return k * ln2Hi - ((hfsq - (s * (hfsq + r) + k * ln2Lo)) - f);
    }
    return k * ln2Hi - ((s * (f - r) - k * ln2Lo) - f);
}

/// A double drawn uniformly from [0, 1): the engine's 64 bits scaled by 2^-64, kept below 1.
inline double unitDouble(Engine &engine) {
#pragma clang fp contract(off)
    const double u = static_cast<double>(engine()) * 0x1.0p-64;
    return u < 1.0 ? u : 0x1.fffffffffffffp-1; // the largest double below 1
}

/// A float drawn uniformly from [0, 1), as unitDouble with float rounding.
inline float unitFloat(Engine &engine) {
#pragma clang fp contract(off)
    const float u = static_cast<float>(engine()) * 0x1.0p-64f;
    return u < 1.0f ? u : 0x1.fffffep-1f; // the largest float below 1
}

/// A double drawn uniformly from [low, high): u * (high - low) + low.
inline double uniformDouble(Engine &engine, double low, double high) {
#pragma clang fp contract(off)
    return unitDouble(engine) * (high - low) + low;
}

/// A float drawn uniformly from [low, high): u * (high - low) + low in float arithmetic.
inline float uniformFloat(Engine &engine, float low, float high) {
#pragma clang fp contract(off)
    return unitFloat(engine) * (high - low) + low;
}

/// An integer drawn uniformly from [0, range), range > 0 (Lemire's nearly divisionless method).
inline uint64_t below(Engine &engine, uint64_t range) {
    uint64_t hi = 0, lo = 0;
    detail::mulWide(engine(), range, hi, lo);
    if (lo < range) {
        const uint64_t threshold = (0 - range) % range; // 2^64 mod range
        while (lo < threshold) {
            detail::mulWide(engine(), range, hi, lo);
        }
    }
    return hi;
}

/// An integer drawn uniformly from [low, high], both included; low <= high.
inline int64_t between(Engine &engine, int64_t low, int64_t high) {
    const uint64_t span = static_cast<uint64_t>(high) - static_cast<uint64_t>(low);
    const uint64_t offset =
        span == std::numeric_limits<uint64_t>::max() ? engine() : below(engine, span + 1);
    return static_cast<int64_t>(static_cast<uint64_t>(low) + offset);
}

/// Draws from the normal distribution with Marsaglia's polar method. One accepted pair (x, y)
/// gives y * m now and x * m on the next draw from the same source.
template <typename Real> class NormalSource {
  public:
    Real operator()(Engine &engine, Real mean = Real(0), Real stddev = Real(1)) {
#pragma clang fp contract(off)
        Real z;
        if (saved_) {
            saved_ = false;
            z      = spare_;
        } else {
            Real x, y, r2;
            do {
                x  = Real(2) * unit(engine) - Real(1);
                y  = Real(2) * unit(engine) - Real(1);
                r2 = x * x + y * y;
            } while (r2 > Real(1) || r2 == Real(0));
            const Real mult = std::sqrt(Real(-2) * log(r2) / r2);
            spare_          = x * mult;
            saved_          = true;
            z               = y * mult;
        }
        return z * stddev + mean;
    }

    /// Forgets the kept second value of a pair.
    void reset() { saved_ = false; }

  private:
    static Real unit(Engine &engine) {
        if constexpr (sizeof(Real) == sizeof(float)) {
            return unitFloat(engine);
        } else {
            return unitDouble(engine);
        }
    }
    static Real log(Real r2) { return static_cast<Real>(random::log(static_cast<double>(r2))); }

    bool saved_ = false;
    Real spare_ = Real(0);
};

} // namespace camel::utils::random
