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
 * CPU feature detection (see cpu.h).
 */

#include "cpu.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace camel::tensor::kernels {

namespace {

#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
CpuFeatures detectCpuFeatures() {
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    auto cpuid = [&](unsigned int leaf, unsigned int sub) {
#if defined(_MSC_VER) && !defined(__clang__)
        int regs[4];
        __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(sub));
        eax = regs[0], ebx = regs[1], ecx = regs[2], edx = regs[3];
#else
        __asm__ __volatile__("cpuid"
                             : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                             : "a"(leaf), "c"(sub));
#endif
    };
    CpuFeatures features;
    cpuid(0, 0);
    if (eax < 7) {
        return features;
    }
    cpuid(1, 0);
    const bool fma     = (ecx & (1u << 12)) != 0;
    const bool osxsave = (ecx & (1u << 27)) != 0;
    const bool avx     = (ecx & (1u << 28)) != 0;
    if (!(osxsave && avx)) {
        return features;
    }
    unsigned int xcr0Low = 0, xcr0High = 0;
    __asm__ __volatile__("xgetbv" : "=a"(xcr0Low), "=d"(xcr0High) : "c"(0));
    const bool ymmState = (xcr0Low & 0x06u) == 0x06u; // XMM, YMM
    const bool zmmState = (xcr0Low & 0xE6u) == 0xE6u; // + opmask, ZMM_Hi256, Hi16_ZMM
    cpuid(7, 0);
    features.avx2Fma = ymmState && fma && (ebx & (1u << 5)) != 0;
    features.avx512f = zmmState && features.avx2Fma && (ebx & (1u << 16)) != 0;
    return features;
}
#else
CpuFeatures detectCpuFeatures() { return {}; }
#endif

} // namespace

const CpuFeatures &cpuFeatures() {
    static const CpuFeatures features = detectCpuFeatures();
    return features;
}

} // namespace camel::tensor::kernels
