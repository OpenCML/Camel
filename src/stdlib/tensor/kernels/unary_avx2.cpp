// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * AVX2 + FMA instantiation of the float unary loops (8-wide). Compiled with AVX2/FMA
 * flags and only called after a runtime CPU check.
 */

#define CAMEL_UNARY_ENTRY unaryF32Avx2
#include "unary_loops.inl"
