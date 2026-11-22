// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/mm/debug_hook.h"

#ifndef NDEBUG

#include "camel/core/debug_breakpoint.h"
#include <atomic>
#include <exception>

namespace camel::core::mm {

namespace {

thread_local unsigned allocationDepth = 0;
thread_local AllocEvent completedAllocation{};

using HookFn = void (*)(const AllocEvent *);
std::atomic<HookFn> &getPostAllocHook() {
    static std::atomic<HookFn> hook{nullptr};
    return hook;
}

} // namespace

AllocationDebugScope::AllocationDebugScope() {
    if (allocationDepth++ == 0)
        completedAllocation = {};
}

AllocationDebugScope::~AllocationDebugScope() noexcept(false) {
    if (--allocationDepth != 0)
        return;
    const auto event    = completedAllocation;
    completedAllocation = {};
    if (event.ptr && std::uncaught_exceptions() == 0)
        camel::DebugBreakpoint::Hit("alloc_boundary", &event);
}

void setPostAllocDebugHook(void (*fn)(const AllocEvent *evt)) { getPostAllocHook().store(fn); }

void clearPostAllocDebugHook() { getPostAllocHook().store(nullptr); }

void invokePreAllocHook(const PreAllocEvent &evt) {
    camel::DebugBreakpoint::Hit("alloc_before", &evt);
}

void invokePostAllocHook(const AllocEvent &evt) {
    if (allocationDepth)
        completedAllocation = evt;
    camel::DebugBreakpoint::Hit("alloc", &evt);
    auto *h = getPostAllocHook().load(std::memory_order_relaxed);
    if (h)
        h(&evt);
}

namespace {

std::atomic<SafepointObserver> &getSafepointObserver() {
    static std::atomic<SafepointObserver> observer{nullptr};
    return observer;
}

} // namespace

void setSafepointObserver(SafepointObserver observer) {
    getSafepointObserver().store(observer, std::memory_order_release);
}

void observeSafepoint(std::string_view reason) {
    SafepointObserver observer = getSafepointObserver().load(std::memory_order_acquire);
    if (observer != nullptr)
        observer(reason);
}

} // namespace camel::core::mm

#else

namespace camel::core::mm {

void invokePreAllocHook(const PreAllocEvent &) {}
void setPostAllocDebugHook(void (*)(const AllocEvent *)) {}
void clearPostAllocDebugHook() {}
void invokePostAllocHook(const AllocEvent &) {}

} // namespace camel::core::mm

#endif
