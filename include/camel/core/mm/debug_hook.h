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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>

namespace camel::core::mm {

/// Pre-allocation event (not allocated yet, so no ptr).
struct PreAllocEvent {
    size_t size;       // Size about to be allocated (including header).
    const char *space; // Space name.
};

/// Post-allocation event information for Web UI display.
struct AllocEvent {
    void *ptr;
    size_t size;       // Total size including header.
    const char *space; // Space name: auto.birth, auto.haven, auto.elder, auto.large, meta, perm
};

/// Invoke breakpoint before allocation (Debug build only); may block until continue.
void invokePreAllocHook(const PreAllocEvent &evt);

/// Set callback invoked after each allocation (Debug build only)
void setPostAllocDebugHook(void (*fn)(const AllocEvent *evt));

/// Clear the post-alloc hook
void clearPostAllocDebugHook();

/// Invoke the hook if set (called from allocator, Debug build only)
void invokePostAllocHook(const AllocEvent &evt);

} // namespace camel::core::mm
