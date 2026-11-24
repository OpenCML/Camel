// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * AVX-512F instantiation of the float unary loops (16-wide). Compiled with AVX-512
 * flags and only called after a runtime CPU check.
 */

#define CAMEL_UNARY_ENTRY unaryF32Avx512
#include "unary_loops.inl"
