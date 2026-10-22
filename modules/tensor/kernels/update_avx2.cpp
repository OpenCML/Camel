// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * AVX2 + FMA instantiation of the fused adaptive update (four doubles per vector). Compiled with
 * AVX2/FMA flags and only called after a runtime CPU check.
 */

#define CAMEL_UPDATE_ENTRY adaptiveUpdateAvx2
#include "update_loops.inl"