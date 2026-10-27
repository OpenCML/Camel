// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/mm/blocking.h"

#include <atomic>

namespace camel::core::mm {

namespace {

std::atomic<BlockingWaitEnter> enterHook{nullptr};
std::atomic<BlockingWaitLeave> leaveHook{nullptr};

} // namespace

void setBlockingWaitHooks(BlockingWaitEnter enter, BlockingWaitLeave leave) {
    // The pair is read and written whole: a scheduler sets it before any worker starts.
    enterHook.store(enter, std::memory_order_release);
    leaveHook.store(leave, std::memory_order_release);
}

void enterBlockingWait() {
    if (BlockingWaitEnter hook = enterHook.load(std::memory_order_acquire)) {
        hook();
    }
}

void leaveBlockingWait() {
    if (BlockingWaitLeave hook = leaveHook.load(std::memory_order_acquire)) {
        hook();
    }
}

} // namespace camel::core::mm