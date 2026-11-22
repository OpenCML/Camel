// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

/// Whether a release is queued right now. Read-only: it neither runs the queue nor changes what a
/// later drain does. A caller that has just queued something, and queued it where no collection
/// will start before the point it is asking about, reads this to tell whether that drain has
/// happened yet.
bool deferredReleasePending() noexcept;

} // namespace camel::core::mm