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
 * Created: Dec. 10, 2025
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "mm.h"

#include "mm.h"

namespace mm { // Memory Management

// 由 GC 系统自动管理，需要从根对象可达
GenerationalAllocatorWithGC &autoSpace() {
    static GenerationalAllocatorWithGC allocator(GenerationalAllocatorWithGC::Config{
        .birthSize             = 32 * MB, // 32 MB
        .havenSize             = 4 * MB,  // 4 MB
        .elderGenSize          = 64 * MB, // 64 MB
        .promotionAgeThreshold = 4,       // 晋升阈值
        .largeObjThreshold     = 4 * KB,  // 大对象阈值
        .minorGCTriggerRatio   = 0.9f,    // 小垃圾回收触发比例
        .majorGCTriggerRatio   = 0.8f     // 大垃圾回收触发比例
    });
    return allocator;
}

// 元数据区，手动管理分配和释放
FreeListAllocator &metaSpace() {
    static FreeListAllocator allocator(16 * MB); // 16 MB
    return allocator;
}

// 永久代，只分配，不释放
BumpPointerAllocator &permSpace() {
    static BumpPointerAllocator allocator(32 * MB); // 32 MB
    return allocator;
}

} // namespace mm
