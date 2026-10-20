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
 * Work that has to run after a collection has finished and the allocator has unlocked.
 *
 * A finalizer runs inside a collection, with the allocator's lock held. Anything it calls may
 * allocate (which needs the lock) or take a lock of its own (the Python GIL), so releasing an
 * external resource there would either deadlock or invert lock order. The collector therefore
 * only queue the release; the thread that reached the safepoint runs the queue once the
 * collection is over (see GenerationalAllocatorWithGC::safepoint). Data blocks use
 * the same queue for the same reason: their finalizer runs under the allocator's lock, while the
 * release takes the block space's own lock.
 */

#pragma once

#include <cstddef>

namespace camel::core::mm {

/// A release callback and the context it frees. Must not throw.
using DeferredRelease = void (*)(void *context) noexcept;

/// Queues `release(context)` to run after the current collection. Never throws: if the queue
/// cannot grow, `release(context)` runs in place (correct, just under the allocator's lock).
void deferRelease(DeferredRelease release, void *context) noexcept;

/// Runs everything queued so far and returns how many releases ran. Called at the three places
/// a collection can start, after the allocator has been unlocked.
size_t runDeferredReleases() noexcept;

} // namespace camel::core::mm