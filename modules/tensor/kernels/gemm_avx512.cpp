// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * AVX-512F SGEMM instantiation: 16-wide vectors, 8 x 32 tiles. This unit is
 * compiled with AVX-512F flags and only called after a runtime CPU check.
 */

#define CAMEL_GEMM_VEC_WIDTH 16
#define CAMEL_GEMM_MR 8
#define CAMEL_GEMM_NR 32
#define CAMEL_GEMM_ENTRY sgemmAvx512
#include "gemm_blocked.inl"
