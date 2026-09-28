/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Vectorizable float transcendentals for elementwise kernels.
 *
 * libm's expf/tanhf are opaque scalar calls, so loops that use them cannot be
 * vectorized. These versions are branch-free straight-line code (clamp, range
 * reduction, polynomial, exponent assembly by bit manipulation), which the
 * compiler turns into SIMD code inside simple loops. Accuracy: exp within a
 * few ulp over the normal float range (Cephes polynomial); tanh, sigmoid and
 * gelu are derived from exp with absolute error around 1e-7. NaN inputs are
 * not propagated faithfully (tensor kernels do not rely on NaN semantics).
 */

#pragma once

#include <bit>
#include <cstdint>
#include <limits>

namespace camel::tensor::kernels::vmath {

/// e^x for float. Overflow gives +inf and underflow gives 0, as with std::exp (results in the
/// subnormal range flush to 0).
inline float exp(float x) {
    constexpr float kMax   = 88.72283905206835f;  // ln(FLT_MAX)
    constexpr float kMin   = -87.33654475055310f; // ln(FLT_MIN)
    constexpr float kLog2e = 1.44269504088896341f;
    constexpr float kC1    = 0.693359375f;    // ln 2 split into a high part ...
    constexpr float kC2    = -2.12194440e-4f; // ... and a low part (Cody-Waite)

    const float c = x > kMax ? kMax : (x < kMin ? kMin : x);

    // n = round(c / ln 2), computed as floor(c * log2e + 0.5) without a libm call.
    const float fx = c * kLog2e + 0.5f;
    float n        = static_cast<float>(static_cast<int32_t>(fx));
    n              = n > fx ? n - 1.0f : n;
    const float r  = c - n * kC1 - n * kC2;
    const float r2 = r * r;
    float p        = 1.9875691500e-4f;
    p              = p * r + 1.3981999507e-3f;
    p              = p * r + 8.3334519073e-3f;
    p              = p * r + 4.1665795894e-2f;
    p              = p * r + 1.6666665459e-1f;
    p              = p * r + 5.0000001201e-1f;
    const float er = p * r2 + r + 1.0f;

    // 2^n with n in [-126, 128] is assembled as 2^n1 * 2^n2 so both factors are normal floats.
    const auto ni        = static_cast<int32_t>(n);
    const int32_t h      = ni >> 1; // arithmetic shift: floor(n / 2)
    const float s1       = std::bit_cast<float>(static_cast<uint32_t>(h + 127) << 23);
    const float s2       = std::bit_cast<float>(static_cast<uint32_t>(ni - h + 127) << 23);
    const float y        = er * s1 * s2;
    constexpr float kInf = std::numeric_limits<float>::infinity();
    return x > kMax ? kInf : (x < kMin ? 0.0f : y);
}

/// tanh(x) = sign(x) * (1 - 2 / (e^{2|x|} + 1)); saturates cleanly for large |x|.
inline float tanh(float x) {
    const float a = x < 0.0f ? -x : x;
    const float t = 1.0f - 2.0f / (vmath::exp(2.0f * a) + 1.0f);
    return x < 0.0f ? -t : t;
}

/// 1 / (1 + e^{-x}).
inline float sigmoid(float x) { return 1.0f / (1.0f + vmath::exp(-x)); }

/// GELU with the tanh approximation.
inline float gelu(float x) {
    constexpr float kScale = 0.7978845608028654f; // sqrt(2 / pi)
    return 0.5f * x * (1.0f + vmath::tanh(kScale * (x + 0.044715f * x * x * x)));
}

} // namespace camel::tensor::kernels::vmath
