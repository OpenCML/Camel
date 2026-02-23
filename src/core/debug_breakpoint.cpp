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
 * Updated: Feb. 24, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/debug_breakpoint.h"

#ifndef NDEBUG

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace camel {

namespace {

struct State {
    std::mutex mtx;
    std::unordered_set<std::string> enabled;
    std::unordered_set<std::string> known_types;
    std::atomic<DebugBreakpoint::HandlerFn> handler{nullptr};
};

State &state() {
    static State s;
    return s;
}

} // namespace

void DebugBreakpoint::SetHandler(HandlerFn fn) {
    state().handler.store(fn, std::memory_order_release);
}

void DebugBreakpoint::EnableType(const char *type) {
    if (!type)
        return;
    std::lock_guard<std::mutex> lock(state().mtx);
    state().enabled.insert(type);
    state().known_types.insert(type);
}

void DebugBreakpoint::DisableType(const char *type) {
    if (!type)
        return;
    std::lock_guard<std::mutex> lock(state().mtx);
    state().enabled.erase(type);
}

bool DebugBreakpoint::IsEnabled(const char *type) {
    if (!type)
        return false;
    std::lock_guard<std::mutex> lock(state().mtx);
    return state().enabled.count(type) != 0;
}

void DebugBreakpoint::RegisterType(const char *type) {
    if (!type)
        return;
    std::lock_guard<std::mutex> lock(state().mtx);
    state().known_types.insert(type);
}

std::vector<std::string> DebugBreakpoint::GetEnabledTypes() {
    std::lock_guard<std::mutex> lock(state().mtx);
    return std::vector<std::string>(state().enabled.begin(), state().enabled.end());
}

std::vector<std::string> DebugBreakpoint::GetKnownTypes() {
    std::lock_guard<std::mutex> lock(state().mtx);
    return std::vector<std::string>(state().known_types.begin(), state().known_types.end());
}

void DebugBreakpoint::Hit(const char *type, const void *context) {
    if (!type)
        return;
    HandlerFn h = state().handler.load(std::memory_order_acquire);
    if (!h)
        return;
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(state().mtx);
        enabled = state().enabled.count(type) != 0;
        state().known_types.insert(type);
    }
    if (enabled)
        h(type, context);
}

} // namespace camel

#else

namespace camel {

void DebugBreakpoint::SetHandler(HandlerFn) {}
void DebugBreakpoint::EnableType(const char *) {}
void DebugBreakpoint::DisableType(const char *) {}
bool DebugBreakpoint::IsEnabled(const char *) { return false; }
void DebugBreakpoint::RegisterType(const char *) {}
std::vector<std::string> DebugBreakpoint::GetEnabledTypes() { return {}; }
std::vector<std::string> DebugBreakpoint::GetKnownTypes() { return {}; }
// Hit() is inline no-op in header for NDEBUG

} // namespace camel

#endif
