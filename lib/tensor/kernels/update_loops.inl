// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Body of the fused adaptive update, compiled once per instruction set (update_generic.cpp,
 * update_avx2.cpp). The including unit defines CAMEL_UPDATE_ENTRY, the exported function name.
 *
 * The loop is straight-line double arithmetic through `adaptiveUpdateOne`, so the compiler
 * vectorizes it to the unit's width. Every unit gives the same bits: the operations are correctly
 * rounded and the unit only decides how many of them one instruction performs.
 */

#include "update.h"

#include <cstdint>

namespace camel::tensor::kernels::detail {

void CAMEL_UPDATE_ENTRY(
    const AdaptiveUpdateRates &rates, const float *param, const float *grad, const float *moment1,
    const float *moment2, float *nextParam, float *nextMoment1, float *nextMoment2, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        double nextP = 0.0, nextM = 0.0, nextV = 0.0;
        adaptiveUpdateOne(rates, param[i], grad[i], moment1[i], moment2[i], nextP, nextM, nextV);
        nextParam[i]   = static_cast<float>(nextP);
        nextMoment1[i] = static_cast<float>(nextM);
        nextMoment2[i] = static_cast<float>(nextV);
    }
}

} // namespace camel::tensor::kernels::detail