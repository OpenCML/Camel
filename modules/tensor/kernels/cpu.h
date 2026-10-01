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
 * Runtime CPU feature detection for the kernels compiled per instruction set
 * (GEMM, elementwise). Units built with wider vector ISAs are only called
 * after these checks confirm both CPU and OS support.
 */

#pragma once

namespace camel::tensor::kernels {

struct CpuFeatures {
    bool avx2Fma = false; // AVX2 + FMA with OS-enabled YMM state
    bool avx512f = false; // AVX-512F with OS-enabled ZMM and opmask state
};

/// Features of the running CPU (detected once; all false off x86-64).
const CpuFeatures &cpuFeatures();

} // namespace camel::tensor::kernels
