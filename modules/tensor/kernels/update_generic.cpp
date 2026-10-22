// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Portable instantiation of the fused adaptive update: the compiler's default target (on x86-64,
 * SSE2, two doubles per vector). Used when the CPU, or CAMEL_TENSOR_ISA=generic, reports no
 * AVX2 + FMA.
 */

#define CAMEL_UPDATE_ENTRY adaptiveUpdateGeneric
#include "update_loops.inl"