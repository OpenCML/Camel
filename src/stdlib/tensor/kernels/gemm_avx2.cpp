// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * AVX2 + FMA SGEMM instantiation: 8-wide vectors, 6 x 16 tiles. This unit is
 * compiled with AVX2/FMA flags and only called after a runtime CPU check.
 */

#define CAMEL_GEMM_VEC_WIDTH 8
#define CAMEL_GEMM_MR 6
#define CAMEL_GEMM_NR 16
#define CAMEL_GEMM_ENTRY sgemmAvx2
#include "gemm_blocked.inl"
