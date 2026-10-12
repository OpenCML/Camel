/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/slot.h"

// Safepoint poisoning: the debugging mode that makes a C++ pointer kept across a safepoint
// fail at once (CAMEL_GC_POISON, docs/technical/32_gc_handle_rules.md).
//
// In this mode every safepoint evacuates the whole young generation, and the collector
// overwrites what the objects left behind with one word, the poison word. The word is the
// address of a page nobody may read, so:
//   - a virtual call through a stale pointer loads the poison as its vtable pointer and
//     faults inside that page;
//   - a stale pointer whose pointee is read as an object reference yields the poison, and the
//     first use of that reference faults the same way;
//   - a scalar read yields a value no program computes (and is caught where it is used as a
//     size or an index).
// The fault handler recognises the page and says what happened instead of a bare crash.

namespace camel::core::mm::poison {

/// Reserves the unreadable page and installs the fault handler. Called once, before the first
/// collection, when the mode is on. Returns the poison word.
slot_t install();

/// The poison word (0 while the mode is off).
slot_t word() noexcept;

/// Whether `value`, read from a slot or used as an address, is the poison (or lies in its page).
bool isPoison(slot_t value) noexcept;

} // namespace camel::core::mm::poison
