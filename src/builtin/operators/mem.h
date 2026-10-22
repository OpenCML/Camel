// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The memory operator the language exposes for diagnostics: `std::mem::blocks()`.
 *
 * It reads the block counters and returns them as a struct, so a program can assert between
 * two operations how many buffers an operation allocated (design principles 10.5; the
 * per-operation allocation tests of H17 step 9). A self-test pass could only report once, at
 * the end; a callable function is read where the question is asked.
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

namespace camel::core::type {
class StructType;
}

/// The struct type of `std::mem::blocks()`: `{ allocs: int, frees: int, live: int, bytes: int }`.
/// One object per process, so the operator's static type and the value it builds are the same
/// type.
camel::core::type::StructType *memBlocksStructType();

slot_t __builtin__mem_blocks__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);