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
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

} // namespace camel::core::mm

#else

namespace camel::core::mm {

void invokePreAllocHook(const PreAllocEvent &) {}
void setPostAllocDebugHook(void (*)(const AllocEvent *)) {}
void clearPostAllocDebugHook() {}
void invokePostAllocHook(const AllocEvent &) {}

} // namespace camel::core::mm

#endif
