// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Float unary loops, compiled once per instruction set (unary_generic.cpp,
 * unary_avx2.cpp, unary_avx512.cpp). The including unit defines
 * CAMEL_UNARY_ENTRY, the exported function name. Every loop body is
 * branch-free straight-line code (vmath.h), so the compiler vectorizes it to
 * the unit's vector width.
 */

#include "elementwise.h"
#include "minmax.h"
#include "vmath.h"

#include <cmath>
#include <cstdint>

namespace camel::tensor::kernels::detail {

namespace {

template <UnaryOp Op> inline float unaryF32(float x) {
    if constexpr (Op == UnaryOp::Neg) {
        return -x;
    } else if constexpr (Op == UnaryOp::Abs) {
        return std::fabs(x);
    } else if constexpr (Op == UnaryOp::Exp) {
        return vmath::exp(x);
    } else if constexpr (Op == UnaryOp::Log) {
        return std::log(x);
    } else if constexpr (Op == UnaryOp::Sqrt) {
        return std::sqrt(x);
    } else if constexpr (Op == UnaryOp::Rsqrt) {
        return 1.0f / std::sqrt(x);
    } else if constexpr (Op == UnaryOp::Sigmoid) {
        return vmath::sigmoid(x);
    } else if constexpr (Op == UnaryOp::Tanh) {
        return vmath::tanh(x);
    } else if constexpr (Op == UnaryOp::Relu) {
        return kernels::relu(x); // maximum(x, +0): NaN stays NaN, -0 becomes +0
    } else if constexpr (Op == UnaryOp::Gelu) {
        return vmath::gelu(x);
    } else {
        static_assert(Op == UnaryOp::Erf);
        return std::erf(x);
    }
}

template <UnaryOp Op> void loop(const float *src, float *dst, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        dst[i] = unaryF32<Op>(src[i]);
    }
}

} // namespace

/// dst[i] = op(src[i]) for i in [0, count), on the calling thread.
void CAMEL_UNARY_ENTRY(UnaryOp op, const float *src, float *dst, int64_t count) {
    switch (op) {
    case UnaryOp::Neg:
        return loop<UnaryOp::Neg>(src, dst, count);
    case UnaryOp::Abs:
        return loop<UnaryOp::Abs>(src, dst, count);
    case UnaryOp::Exp:
        return loop<UnaryOp::Exp>(src, dst, count);
    case UnaryOp::Log:
        return loop<UnaryOp::Log>(src, dst, count);
    case UnaryOp::Sqrt:
        return loop<UnaryOp::Sqrt>(src, dst, count);
    case UnaryOp::Rsqrt:
        return loop<UnaryOp::Rsqrt>(src, dst, count);
    case UnaryOp::Sigmoid:
        return loop<UnaryOp::Sigmoid>(src, dst, count);
    case UnaryOp::Tanh:
        return loop<UnaryOp::Tanh>(src, dst, count);
    case UnaryOp::Relu:
        return loop<UnaryOp::Relu>(src, dst, count);
    case UnaryOp::Gelu:
        return loop<UnaryOp::Gelu>(src, dst, count);
    case UnaryOp::Erf:
        return loop<UnaryOp::Erf>(src, dst, count);
    }
}

} // namespace camel::tensor::kernels::detail
