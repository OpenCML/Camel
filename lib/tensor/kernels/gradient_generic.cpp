// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Portable instantiation of the fused GELU gradient: the compiler's default target (on x86-64,
 * SSE2, four floats per vector). Used when the CPU, or `--tensor-isa generic`, reports no
 * AVX2 + FMA. It is compiled with the project's -ffp-contract=off and takes no per-ISA option of
 * its own, so the same source gives the same bits on every target -- including the arm64 baseline,
 * which has an FMA instruction.
 */

#define CAMEL_GELU_GRAD_ENTRY geluGradGeneric
#include "gradient_loops.inl"
