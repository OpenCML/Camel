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

#pragma once

#include <cstddef>

namespace mm {

/// 分配前事件（尚未分配，无 ptr）
struct PreAllocEvent {
    size_t size;       // 即将分配的大小（含 header）
    const char *space; // 空间名
};

/// 分配后事件信息，供 Web UI 展示
struct AllocEvent {
    void *ptr;
    size_t size;       // 含 header 的总大小
    const char *space; // 空间名：auto.birth, auto.haven, auto.elder, auto.large, meta, perm
};

/// Invoke breakpoint before allocation (Debug build only); may block until continue.
void invokePreAllocHook(const PreAllocEvent &evt);

/// Set callback invoked after each allocation (Debug build only)
void setPostAllocDebugHook(void (*fn)(const AllocEvent *evt));

/// Clear the post-alloc hook
void clearPostAllocDebugHook();

/// Invoke the hook if set (called from allocator, Debug build only)
void invokePostAllocHook(const AllocEvent &evt);

} // namespace mm
