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
 * Created: Nov. 07, 2025
 * Updated: Nov. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "gen.h"

namespace mm { // Memory Management
// 由 GC 系统自动管理，需要从根对象可达
GenerationAllocatorUsingGC &autoSpace() {
    static GenerationAllocatorUsingGC allocator(GenerationAllocatorUsingGC::Config{
        .edenSize              = 1024 * 1024 * 16, // 16 MB
        .survivorSize          = 1024 * 1024 * 2,  // 2 MB
        .promotionAgeThreshold = 2                 // 晋升阈值
    });
    return allocator;
}

// 元数据区，手动管理分配和释放
FreeListAllocator &metaSpace() {
    static FreeListAllocator allocator(1024 * 1024 * 8); // 8 MB
    return allocator;
}

// 永久代，只分配，不释放
BumpPointerAllocator &permSpace() {
    static BumpPointerAllocator allocator(1024 * 1024 * 16); // 16 MB
    return allocator;
}
} // namespace mm
