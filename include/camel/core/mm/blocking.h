// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/// A blocking wait, announced to the runtime (C24, MG/R5/BE/6).
///
/// A cooperative stop-the-world collection runs only while no other thread is inside a GC region.
/// Regions are leaf work, so a thread normally never waits while it holds one -- but an operator
/// that blocks (a sleep, a device synchronise, a lock taken across a wait) does wait, and for as
/// long as it waits nothing can be collected: the collector waits 500 ms, gives up, and the heap
/// grows uncollected. Saying so around the wait lets the scheduler leave the region for the wait
/// and re-enter it after it, so a blocked worker is not what stops the world.
///
/// The scheduler registers the hooks (`setBlockingWaitHooks`); a wait is written as
///
///     mm::BlockingWaitScope wait;   // one scope per wait
///
/// and costs one relaxed load and two calls when no hooks are registered (every other back end,
/// and every serial run).
namespace camel::core::mm {

using BlockingWaitEnter = void (*)();
using BlockingWaitLeave = void (*)();

/// Registers the pair, or clears it with two nulls. One process-level pair: the runtime has one
/// scheduler per process at a time.
void setBlockingWaitHooks(BlockingWaitEnter enter, BlockingWaitLeave leave);

/// Says a wait is starting / has ended. Without hooks both are one relaxed load.
void enterBlockingWait();
void leaveBlockingWait();

/// The pair around one wait. Nested scopes enter and leave once each; the hooks see every one.
struct BlockingWaitScope {
    BlockingWaitScope() { enterBlockingWait(); }
    ~BlockingWaitScope() { leaveBlockingWait(); }
    BlockingWaitScope(const BlockingWaitScope &)            = delete;
    BlockingWaitScope &operator=(const BlockingWaitScope &) = delete;
};

} // namespace camel::core::mm