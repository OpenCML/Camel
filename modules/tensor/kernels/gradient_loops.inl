// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Body of the fused GELU gradient, compiled once per instruction set (gradient_generic.cpp,
 * gradient_avx2.cpp). The including unit defines CAMEL_GELU_GRAD_ENTRY, the exported name.
 *
 *   u     = scale * (v + cubic * v * v * v)                      scale = sqrt(2/pi)
 *   t     = tanh(u)
 *   slope = 0.5 * (1 + t) + 0.5 * v * (1 - t * t) * scale * (1 + 3 * cubic * v * v)
 *   out   = dy * slope
 *
 * Written as operators this is three passes over whole tensors and two temporaries (the
 * polynomial, and the tangent of it); this loop keeps the same arithmetic and reads each input
 * once. Every value is the one the three passes produced:
 *
 *   - the polynomial and the slope are rounded as written, in both units. The operator chain wrote
 *     them with the project's -ffp-contract=off, so the pragma below keeps them uncontracted
 *     inside a unit that is compiled with contraction on. Nothing here is a sum: there is no
 *     grouping to change and no chain to re-associate.
 *   - tanh is the same `vmath::tanh` the unary kernel inlines, contracted the same way: the unary
 *     units are the project's one place that opts into contraction (modules/tensor/
 *     CMakeLists.txt), and these units carry that setting for the tangent alone. The tangent's
 *     rounding is therefore the unary units' -- identical on the same build and the same CPU, and
 *     inside the function's declared error bound across processors with and without FMA, which is
 *     what design principles 7.1 promises of a transcendental.
 *
 * An element is independent of its neighbours, so which unit runs, and how the range is split
 * across threads, changes neither.
 */

#include "vmath.h"

#include <cstdint>

// From here on, products are rounded before they meet an add: the polynomial and the slope are
// the operator chain's arithmetic, and that chain ran with the project's -ffp-contract=off.
// `vmath::tanh` above is parsed under the unit's own setting (contraction on), which is the
// setting the unary unit that computed it before is compiled with.
#pragma clang fp contract(off)

namespace camel::tensor::kernels::detail {

namespace {

constexpr float kScale = 0.7978845608028654f; // sqrt(2 / pi)
constexpr float kCubic = 0.044715f;

// The leaning of the logistic term, written as the operator chain wrote it: every product is
// rounded before it meets the next add.
inline float geluPolynomial(float v) { return kScale * (v + kCubic * v * v * v); }

inline float geluSlope(float v, float t) {
    return 0.5f * (1.0f + t) + 0.5f * v * (1.0f - t * t) * kScale * (1.0f + 3.0f * kCubic * v * v);
}

} // namespace

void CAMEL_GELU_GRAD_ENTRY(const float *x, const float *dy, float *out, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        const float v = x[i];
        out[i]        = dy[i] * geluSlope(v, vmath::tanh(geluPolynomial(v)));
    }
}

} // namespace camel::tensor::kernels::detail
