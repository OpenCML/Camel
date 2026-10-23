// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Portable instantiation of the fused GELU gradient: the compiler's default target (on x86-64,
 * SSE2, four floats per vector). Used when the CPU, or CAMEL_TENSOR_ISA=generic, reports no
 * AVX2 + FMA.
 */

#define CAMEL_GELU_GRAD_ENTRY geluGradGeneric
#include "gradient_loops.inl"
