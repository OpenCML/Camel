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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm.h"

namespace camel::core::mm {

// Managed automatically by the GC system and must remain reachable from roots.
GenerationalAllocatorWithGC &autoSpace() {
    static auto *allocator = new GenerationalAllocatorWithGC(
        GenerationalAllocatorWithGC::Config{
            .birthSize             = 32 * MB, // 32 MB
            .havenSize             = 4 * MB,  // 4 MB
            .elderGenSize          = 64 * MB, // 64 MB
            .promotionAgeThreshold = 4,       // Promotion threshold.
            .largeObjThreshold     = 4 * KB,  // Large-object threshold.
            .minorGCTriggerRatio   = 0.9f,    // Minor GC trigger ratio.
            .majorGCTriggerRatio   = 0.8f     // Major GC trigger ratio.
        });
    return *allocator;
}

// Metadata space: manually managed allocation and release.
FreeListAllocator &metaSpace() {
    // The process-level meta space uses an intentionally-leaked singleton to avoid static
    // destruction order problems: some global objects may still call metaSpace().free() during
    // shutdown.
    static auto *allocator = new FreeListAllocator(16 * MB, "meta"); // 16 MB
    return *allocator;
}

// Permanent generation: allocate only, never free.

FreeListAllocator &graphSpace() {
    static auto *allocator = new FreeListAllocator(64 * MB, "graph"); // 64 MB
    return *allocator;
}
BumpPointerAllocator &permSpace() {
    static auto *allocator = new BumpPointerAllocator(32 * MB, "perm"); // 32 MB
    return *allocator;
}

} // namespace camel::core::mm
