// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/observer.h"

#include <atomic>

namespace camel::execute {

namespace {
// Constant-initialised: no dynamic initialisation, so nothing runs when the library loads.
std::atomic<ExecutionObserver *> gObserver{nullptr};
} // namespace

ExecutionObserver *executionObserver() noexcept {
    return gObserver.load(std::memory_order_relaxed);
}

void setExecutionObserver(ExecutionObserver *observer) noexcept {
    gObserver.store(observer, std::memory_order_release);
}

} // namespace camel::execute
