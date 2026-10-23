// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A fused elementwise update of a parameter tensor and its two moment tensors from the gradient.
 *
 * The optimizer in `nn` advances one parameter tree per step. Written with the elementwise tensor
 * operators this update would be five to seven passes over whole tensors, and because a tensor
 * stores float32 every intermediate would be rounded to float. The scalar loop it replaces kept
 * the arithmetic in double and rounded once, on the store; this kernel is one pass that does the
 * same, element by element:
 *
 *   nextM = beta1 * m + (1 - beta1) * g
 *   nextV = beta2 * v + (1 - beta2) * g * g
 *   nextP = p - lr * (nextM / correct1) / (sqrt(nextV / correct2) + eps)
 *
 * evaluated in exactly that order with every operation rounded as written (the project compiles
 * with -ffp-contract=off), so the values are bit for bit those of the loop it replaced.
 * `correct1` and `correct2` are the bias corrections 1 - beta1^step and 1 - beta2^step, which the
 * caller computes once per step.
 *
 * Every element is independent, so a range split across threads gives the same bits as an inline
 * range. The kernel is a tensor-level operation -- four tensors in, three out -- because that is
 * where the grain policy and the per-instruction-set units live; which optimizer drives it is the
 * caller's business.
 */

#pragma once

#include <cstdint>

namespace camel::tensor::kernels {

/// The constants of one update. `correct1`, `correct2` are 1 - beta1^step, 1 - beta2^step.
struct AdaptiveUpdateRates {
    double lr;
    double beta1;
    double beta2;
    double eps;
    double correct1;
    double correct2;
};

/**
 * The update of one element, in double.
 *
 * This is the definition of the operation: the loop below and a caller that holds a single value
 * (a non-tensor leaf of an optimizer's tree) both call it, so the formula is written once.
 *
 * `__builtin_elementwise_sqrt` rather than `std::sqrt`: the latter compiles to a libm call (it may
 * set errno), which costs about 5.6 ns per element on the reference host and stops the compiler
 * from vectorizing the loop at all. The builtin is the IEEE square root instruction, correctly
 * rounded and therefore the same number, without the call.
 */
inline void adaptiveUpdateOne(
    const AdaptiveUpdateRates &r, double param, double grad, double moment1, double moment2,
    double &nextParam, double &nextMoment1, double &nextMoment2) {
    const double m = r.beta1 * moment1 + (1.0 - r.beta1) * grad;
    const double v = r.beta2 * moment2 + (1.0 - r.beta2) * grad * grad;
    nextParam =
        param - r.lr * (m / r.correct1) / (__builtin_elementwise_sqrt(v / r.correct2) + r.eps);
    nextMoment1 = m;
    nextMoment2 = v;
}

/**
 * One pass over whole tensors: `nextParam`, `nextMoment1` and `nextMoment2` receive the updated
 * parameter tensor and moments, read from `param`, `grad`, `moment1` and `moment2`, all of
 * `count` contiguous float32 elements. Every output is a fresh buffer and must not overlap any
 * input (the inputs may be distinct buffers, which they are: the caller allocates the outputs).
 *
 * The range is split by the tensor module's grain policy (`parallel.h`), with the serial cost of
 * one element measured per unit: the AVX2 unit runs at 1.56 ns per element, the generic one at
 * 3.08 ns, so one chunk is 40 us of serial work either way. A range with fewer than two chunks
 * runs inline on the calling thread and does not create the pool.
 */
void adaptiveUpdate(
    int64_t count, const AdaptiveUpdateRates &rates, const float *param, const float *grad,
    const float *moment1, const float *moment2, float *nextParam, float *nextMoment1,
    float *nextMoment2);

/**
 * The same update for a leaf whose element type is f64: `count` contiguous double elements per
 * stream, each one through `adaptiveUpdateOne` exactly as the pass above does it. Nothing is
 * narrowed on the way in or on the way out, so an f64 leaf keeps f64 (design principles 7.4: the
 * element type is exact; `nn`'s leaf keeps the element type its type states). The arithmetic per
 * element is the same double work the f32 pass does -- only the loads and stores are twice as
 * wide -- and the range is split by the same grain policy at the portable unit's rate, because the
 * per-ISA units below are the f32 ones. A per-ISA double unit is a speed decision, not a semantic
 * one, and the values are the portable unit's either way.
 */
void adaptiveUpdate64(
    int64_t count, const AdaptiveUpdateRates &rates, const double *param, const double *grad,
    const double *moment1, const double *moment2, double *nextParam, double *nextMoment1,
    double *nextMoment2);

namespace detail {

using AdaptiveUpdateFn = void (*)(
    const AdaptiveUpdateRates &, const float *, const float *, const float *, const float *,
    float *, float *, float *, int64_t);

/// Portable unit: the compiler's default target (on x86-64, SSE2: two doubles per vector).
void adaptiveUpdateGeneric(
    const AdaptiveUpdateRates &, const float *, const float *, const float *, const float *,
    float *, float *, float *, int64_t);

#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
/// AVX2 + FMA unit (four doubles per vector), only called after a runtime CPU check.
void adaptiveUpdateAvx2(
    const AdaptiveUpdateRates &, const float *, const float *, const float *, const float *,
    float *, float *, float *, int64_t);
#endif

} // namespace detail

} // namespace camel::tensor::kernels