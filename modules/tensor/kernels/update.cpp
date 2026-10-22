// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The fused adaptive update: which unit runs, and how the range is split.
 *
 * The rate below is the serial cost of one element, measured on the reference host, and it is
 * what turns the grain policy into elements: one chunk is kMinWorkPerWorkerNs of this kernel's
 * own work, and a range that holds fewer than two such chunks runs inline (parallelFor) without
 * creating the pool. The unit carries its own rate because the same number of elements costs
 * about twice as much in the portable unit as in the AVX2 one.
 */

#include "update.h"

#include "cpu.h"
#include "parallel.h"

#include <cstdint>

namespace camel::tensor::kernels {

namespace {

/// Serial cost of one element, measured on the reference host (32-core EPYC 7343, Release,
/// medians of 300 rounds over 101770 elements, load average 16-31):
///
///   generic unit (SSE2, two doubles per vector)   3.08 ns   313 us
///   AVX2 unit (four doubles per vector)           1.56 ns   159 us
///
/// A plain float pass over the same buffers is kNsPerFloat (0.17 ns); the rest is the update's
/// arithmetic -- three correctly rounded double divisions and one double square root per element
/// -- which is the floor for a form that keeps the values bit for bit.
constexpr double kNsPerElementGeneric = 3.08;
constexpr double kNsPerElementAvx2    = 1.56;

struct AdaptiveUpdateUnit {
    detail::AdaptiveUpdateFn run;
    double nsPerElement;
};

/// The unit for this CPU, chosen once (the same check the other per-ISA kernels use).
AdaptiveUpdateUnit adaptiveUpdateUnit() {
    static const AdaptiveUpdateUnit unit = [] {
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
        if (cpuFeatures().avx2Fma) {
            return AdaptiveUpdateUnit{&detail::adaptiveUpdateAvx2, kNsPerElementAvx2};
        }
#endif
        return AdaptiveUpdateUnit{&detail::adaptiveUpdateGeneric, kNsPerElementGeneric};
    }();
    return unit;
}

} // namespace

void adaptiveUpdate(
    int64_t count, const AdaptiveUpdateRates &rates, const float *param, const float *grad,
    const float *moment1, const float *moment2, float *nextParam, float *nextMoment1,
    float *nextMoment2) {
    // One pointer is captured rather than eight: a range that splits turns the body into a
    // std::function for the pool, and a capture that fits its small buffer does not allocate
    // (the per-call allocation H54 removed from the inline path, here on the split path only).
    struct Streams {
        detail::AdaptiveUpdateFn run;
        const AdaptiveUpdateRates *rates;
        const float *param;
        const float *grad;
        const float *moment1;
        const float *moment2;
        float *nextParam;
        float *nextMoment1;
        float *nextMoment2;
    };
    const AdaptiveUpdateUnit unit = adaptiveUpdateUnit();
    const Streams streams{
        unit.run,
        &rates,
        param,
        grad,
        moment1,
        moment2,
        nextParam,
        nextMoment1,
        nextMoment2};
    parallelFor(count, grainForWork(unit.nsPerElement), [&streams](int64_t begin, int64_t end) {
        streams.run(
            *streams.rates,
            streams.param + begin,
            streams.grad + begin,
            streams.moment1 + begin,
            streams.moment2 + begin,
            streams.nextParam + begin,
            streams.nextMoment1 + begin,
            streams.nextMoment2 + begin,
            end - begin);
    });
}

} // namespace camel::tensor::kernels