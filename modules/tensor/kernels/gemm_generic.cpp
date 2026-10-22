// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Portable SGEMM instantiation: 4-wide vectors (SSE2 / NEON baseline), 4 x 8 tiles.
 */

#define CAMEL_GEMM_VEC_WIDTH 4
#define CAMEL_GEMM_MR 4
#define CAMEL_GEMM_NR 8
#define CAMEL_GEMM_ENTRY sgemmGeneric
#include "gemm_blocked.inl"
