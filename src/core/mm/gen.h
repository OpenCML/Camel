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
#include "alloc/large_obj.h"
#include "gc.h"
#include "utils/assert.h"

class GenerationalAllocatorUsingGC : public IAllocator {
  private:
    // 年轻代
    BumpPointerAllocator eden_;
    BumpPointerAllocator from_;
    BumpPointerAllocator to_;

    // 老年代
    FreeListAllocator oldGen_;

    // 大对象专用分配器
    LargeObjectAllocator largeObjSpace_;

    size_t promotionAgeThreshold_;

    GarbageCollector gc_;

  public:
    struct Config {
        size_t edenSize;              // Eden 空间大小
        size_t survivorSize;          // 单个 Survivor 区大小
        size_t oldGenSize;            // 老年代大小
        size_t promotionAgeThreshold; // 晋升阈值
    };

    GenerationalAllocatorUsingGC(const Config &config)
        : eden_(config.edenSize), from_(config.survivorSize), to_(config.survivorSize),
          oldGen_(config.oldGenSize), largeObjSpace_(),
          promotionAgeThreshold_(config.promotionAgeThreshold) {}

    // 普通对象分配
    void *alloc(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        // 大对象直接走大对象分配器
        if (__builtin_expect(payloadSize > 1024, 0)) {
            void *ptr = largeObjSpace_.alloc(payloadSize, align);
            if (__builtin_expect(!ptr, 0))
                throw std::bad_alloc();
            return ptr;
        }

        // 尝试在 Eden 分配
        void *ptr = eden_.alloc(payloadSize, align);
        if (__builtin_expect(!ptr, 0)) {
            minorGC();
            ptr = eden_.alloc(payloadSize, align);
            if (__builtin_expect(!ptr, 0))
                throw std::bad_alloc();
        }
        return ptr;
    }

    void free(void *ptr) {
        // GC 管理生命周期，不支持手动释放
        ASSERT(false, "GenerationalAllocatorUsingGC does not support manual free");
    }

    GarbageCollector &gc() { return gc_; }

    // Minor GC：只清理 Eden + Survivor From
    void minorGC() {}

    // Major GC：清理老年代
    void majorGC() { ASSERT(false, "Major GC not implemented yet"); }

  private:
    void *copyOrPromote(void *objPtr) {
        if (!objPtr)
            return nullptr;

        auto *header =
            reinterpret_cast<ObjectHeader *>(static_cast<uint8_t *>(objPtr) - sizeof(ObjectHeader));

        if (header->forwarded()) {
            return header->addr(); // 已复制，直接返回新地址
        }

        size_t payloadSize = header->size() - sizeof(ObjectHeader);
        header->age++;

        void *newDataPtr = nullptr;
        if (header->age >= promotionAgeThreshold_) {
            // 晋升到老年代
            newDataPtr = oldGen_.alloc(payloadSize, alignof(std::max_align_t));
        } else {
            // 复制到 Survivor To
            newDataPtr = to_.alloc(payloadSize, alignof(std::max_align_t));
        }

        if (!newDataPtr)
            throw std::bad_alloc();

        // 复制 payload 区域
        std::memcpy(newDataPtr, objPtr, payloadSize);

        // 设置转发地址（指向新数据区）
        header->forward(newDataPtr);

        return newDataPtr;
    }
};
