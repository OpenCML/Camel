// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * AVX2 + FMA instantiation of the fused GELU gradient (eight floats per vector). Compiled with
 * AVX2/FMA flags and only called after a runtime CPU check.
 *
 * Contraction stays on in this unit, the setting the unary kernel that computed the tangent
 * before is compiled with, so the tangent's bits are unchanged.
 */

#define CAMEL_GELU_GRAD_ENTRY geluGradAvx2
#include "gradient_loops.inl"
