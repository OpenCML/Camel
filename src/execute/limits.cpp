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
 * Created: Oct. 12, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/execute/limits.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <format>
#include <string>
#include <system_error>

#if defined(__linux__) && defined(__GLIBC__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace camel::execute {

namespace {

std::optional<size_t> depthLimitFromEnvironment(const char *name) {
    const char *raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    return parseDepthLimit(raw);
}

struct StackBounds {
    uintptr_t low  = 0; // 0: unknown
    uintptr_t high = 0;
};

// The bounds of the calling thread's native stack, cached independently of the call count.
StackBounds readStackBounds() {
    StackBounds bounds;
#if defined(__linux__) && defined(__GLIBC__)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) != 0) {
        return bounds;
    }
    void *addr  = nullptr;
    size_t size = 0;
    if (pthread_attr_getstack(&attr, &addr, &size) == 0 && addr != nullptr && size != 0) {
        bounds.low  = reinterpret_cast<uintptr_t>(addr);
        bounds.high = bounds.low + size;
    }
    pthread_attr_destroy(&attr);
#elif defined(_WIN32)
    ULONG_PTR low  = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    bounds.low  = static_cast<uintptr_t>(low);
    bounds.high = static_cast<uintptr_t>(high);
#elif defined(__APPLE__)
    const pthread_t self = pthread_self();
    const size_t size    = pthread_get_stacksize_np(self);
    void *top            = pthread_get_stackaddr_np(self); // the high end
    if (top != nullptr && size != 0) {
        bounds.high = reinterpret_cast<uintptr_t>(top);
        bounds.low  = bounds.high - size;
    }
#endif
    return bounds;
}

const StackBounds &threadStackBounds() {
    thread_local const StackBounds bounds = readStackBounds();
    return bounds;
}

// An address inside the frame of the function that asks.
uintptr_t stackHere() {
    volatile char marker = 0;
    return reinterpret_cast<uintptr_t>(&marker);
}

// Stack kept free for the nested call that passed the check, the operators it calls, and the
// construction and unwinding of the diagnostic. Windows SEH needs a larger reserve than Unix.
size_t stackReserve(const StackBounds &bounds) {
#if defined(_WIN32)
    // Recursive Windows SEH rethrows keep handler frames live until an outer catch handles
    // the fault. Keep the reserve validated by the parallel recursion regression test.
    return std::max<size_t>((bounds.high - bounds.low) / 16 * 15, 128 * 1024);
#else
    return std::max<size_t>((bounds.high - bounds.low) / 16, 128 * 1024);
#endif
}

} // namespace

std::optional<size_t> parseDepthLimit(std::string_view text) {
    size_t value   = 0;
    const char *b  = text.data();
    const char *e  = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(b, e, value);
    if (text.empty() || ec != std::errc() || ptr != e || value == 0 || value > kMaxDepthLimit) {
        return std::nullopt;
    }
    return value;
}

RecursionLimit recursionLimit(const core::context::Context &context) {
    if (const size_t option = context.maxRecursionDepth(); option != 0) {
        return {std::min(option, kMaxDepthLimit), true};
    }
    static const std::optional<size_t> fromEnvironment =
        depthLimitFromEnvironment(kMaxRecursionDepthEnvVar);
    if (fromEnvironment) {
        return {*fromEnvironment, true};
    }
    return {kDefaultMaxRecursionDepth, false};
}

size_t macroDepthLimit(const core::context::Context &context) {
    if (const size_t option = context.maxMacroDepth(); option != 0) {
        return std::min(option, kMaxDepthLimit);
    }
    static const std::optional<size_t> fromEnvironment =
        depthLimitFromEnvironment(kMaxMacroDepthEnvVar);
    return fromEnvironment.value_or(kDefaultMaxMacroDepth);
}

std::optional<size_t> parseRoundLimit(std::string_view text) {
    size_t value   = 0;
    const char *b  = text.data();
    const char *e  = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(b, e, value);
    if (text.empty() || ec != std::errc() || ptr != e || value == 0 || value > kMaxRoundsLimit) {
        return std::nullopt;
    }
    return value;
}

size_t macroRoundLimit(const core::context::Context &context) {
    if (const size_t option = context.maxMacroRounds(); option != 0) {
        return std::min(option, kMaxRoundsLimit);
    }
    static const std::optional<size_t> fromEnvironment = [] {
        const char *raw = std::getenv(kMaxMacroRoundsEnvVar);
        return raw == nullptr ? std::nullopt : parseRoundLimit(raw);
    }();
    return fromEnvironment.value_or(kDefaultMaxMacroRounds);
}

size_t framePoolBytes(size_t limit) {
    constexpr size_t kBytesPerCall = 4096;
    constexpr size_t kMaxBytes     = size_t{256} * 1024 * 1024;
    return std::clamp(limit * kBytesPerCall, kDefaultMaxRecursionDepth * kBytesPerCall, kMaxBytes);
}

void throwRecursionLimitReached(std::string_view graphName, size_t limit) {
    core::error::throwRuntimeFault(
        core::error::RuntimeDiag::MaxRecursionDepthExceeded,
        std::string(graphName),
        limit);
}

std::string nativeStackExhaustedDetail(size_t openCalls) {
    return std::format(
        "{} open calls: the native stack of the thread, {} KiB, is full",
        openCalls,
        nativeStackSize() / 1024);
}

void throwNativeStackExhausted(std::string_view graphName, size_t openCalls) {
    core::error::throwRuntimeFault(
        core::error::RuntimeDiag::MaxRecursionDepthExceeded,
        std::string(graphName),
        nativeStackExhaustedDetail(openCalls));
}

uintptr_t nativeStackFloor() {
    const StackBounds &bounds = threadStackBounds();
    if (bounds.low == 0 || !nativeStackGuardEnabled()) {
        return 0;
    }
    return bounds.low + stackReserve(bounds);
}

namespace {
// Set once by --native-stack-guard, before anything runs; on by default.
std::atomic<bool> &nativeStackGuardFlag() {
    static std::atomic<bool> flag{true};
    return flag;
}
} // namespace

bool nativeStackGuardEnabled() {
    return nativeStackGuardFlag().load(std::memory_order_relaxed);
}

void setNativeStackGuardEnabled(bool enabled) {
    nativeStackGuardFlag().store(enabled, std::memory_order_relaxed);
}

void nativeStackBounds(uintptr_t &low, uintptr_t &high) {
    const StackBounds &bounds = threadStackBounds();
    low                       = bounds.low;
    high                      = bounds.high;
}

bool nativeStackNearlyExhausted() {
    const StackBounds &bounds = threadStackBounds();
    if (bounds.low == 0 || !nativeStackGuardEnabled()) {
        return false;
    }
    const uintptr_t here = stackHere();
    if (here <= bounds.low || here > bounds.high) {
        return false; // not on the stack the bounds describe: nothing can be said
    }
    return here - bounds.low < stackReserve(bounds);
}

size_t nativeStackUsed() {
    const StackBounds &bounds = threadStackBounds();
    const uintptr_t here      = stackHere();
    if (bounds.low == 0 || here <= bounds.low || here > bounds.high) {
        return 0;
    }
    return bounds.high - here;
}

size_t nativeStackSize() {
    const StackBounds &bounds = threadStackBounds();
    return bounds.high - bounds.low;
}

} // namespace camel::execute
