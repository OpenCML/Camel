/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The memory operator the language exposes for diagnostics: `std::mem::buffers()`.
 *
 * It reads the buffer counters and returns them as a struct, so a program can assert between
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

/// The struct type of `std::mem::buffers()`: `{ allocs: int, frees: int, live: int, bytes: int }`.
/// One object per process, so the operator's static type and the value it builds are the same
/// type.
camel::core::type::StructType *memBuffersStructType();

slot_t __builtin__mem_buffers__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);