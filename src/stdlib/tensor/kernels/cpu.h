// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Runtime CPU feature detection for the kernels compiled per instruction set
 * (GEMM, elementwise). Units built with wider vector ISAs are only called
 * after these checks confirm both CPU and OS support.
 *
 * `--tensor-isa generic` hides every feature, so the portable units run
 * on any processor. Tests use it to check that the GEMM gives the same bits
 * in every unit (design principles 7.1).
 */

#pragma once

namespace camel::tensor::kernels {

struct CpuFeatures {
    bool avx2Fma = false; // AVX2 + FMA with OS-enabled YMM state
    bool avx512f = false; // AVX-512F with OS-enabled ZMM and opmask state
};

/// Features of the running CPU (detected once; all false off x86-64 and under
/// `--tensor-isa generic`).
const CpuFeatures &cpuFeatures();

} // namespace camel::tensor::kernels
