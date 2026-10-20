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
 * The operators a lowered collecting loop uses (camel/runtime/loop.h, "Collection slots"):
 * the machinery of the tape, inserted by the runtime; a program cannot name it.
 *
 * `:loop/collector` (no operands; the result type is the slot's ArrayType): a fresh empty
 * array. It is the loop's first tape per slot.
 *
 * `:loop/collect` (Norm operands: the tape, the value; the result is the tape): appends the
 * value to the tape with the slot's element type, in place and O(1) amortized (Array::append
 * grows geometrically). The tape belongs to the loop: it was made by `:loop/collector` in this
 * activation, is carried only in the loop's state, and is returned as part of the loop's value,
 * so the in-place append cannot be observed by anything else. A memory exhausted while growing
 * the tape is reported by the pass runner at the node the VM was running.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/operator.h"

slot_t __loop_collector__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __loop_collect__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);