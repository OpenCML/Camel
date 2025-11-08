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
 * Updated: Nov. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/large_obj.h"
#include "utils/assert.h"

struct GCConfig {
    size_t edenSize;              // Eden 空间大小
    size_t survivorSize;          // 单个 Survivor 区大小
    size_t oldGenSize;            // 老年代大小
    size_t permGenSize;           // 永久代大小
    size_t promotionAgeThreshold; // 晋升阈值
};

class GarbageCollector {
  private:
    // 年轻代
    BumpPointerAllocator eden;
    BumpPointerAllocator survivorFrom;
    BumpPointerAllocator survivorTo;

    // 老年代
    FreeListAllocator oldGen;

    // 持久代
    FreeListAllocator permGen;

    // 大对象专用分配器
    LargeObjectAllocator largeObj;

    size_t promotionAgeThreshold = 2;
    std::vector<void *> rootSet; // 模拟根集合

  public:
    GarbageCollector(const GCConfig &config)
        : eden(config.edenSize), survivorFrom(config.survivorSize), survivorTo(config.survivorSize),
          oldGen(config.oldGenSize), permGen(config.permGenSize), largeObj(),
          promotionAgeThreshold(config.promotionAgeThreshold) {}

    // 添加根对象（模拟）
    void addRoot(void *obj) { rootSet.push_back(obj); }

    // 普通对象分配
    void *allocate(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        // 大对象直接走大对象分配器
        if (__builtin_expect(payloadSize > 1024, 0)) {
            void *ptr = largeObj.alloc(payloadSize, align);
            if (__builtin_expect(!ptr, 0))
                throw std::bad_alloc();
            return ptr;
        }

        // 尝试在 Eden 分配
        void *ptr = eden.alloc(payloadSize, align);
        if (__builtin_expect(!ptr, 0)) {
            minorGC();
            ptr = eden.alloc(payloadSize, align);
            if (__builtin_expect(!ptr, 0))
                throw std::bad_alloc();
        }
        return ptr;
    }

    // 持久代分配
    void *allocatePermanent(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        void *ptr = permGen.alloc(payloadSize, align);
        if (!ptr)
            throw std::bad_alloc();
        return ptr;
    }

    // Minor GC：只清理 Eden + Survivor From
    void minorGC() {
        survivorTo.reset();

        // 模拟 root 集合扫描
        for (void *&rootObj : rootSet) {
            rootObj = copyOrPromote(rootObj); // 更新根指针
        }

        // 交换 Survivor From / To
        std::swap(survivorFrom, survivorTo);

        // 清空 Eden
        eden.reset();
    }

    // Major GC：清理老年代
    void majorGC() { ASSERT(false, "Major GC not implemented yet"); }

  private:
    void *copyOrPromote(void *objPtr) {
        if (!objPtr)
            return nullptr;

        auto *header =
            reinterpret_cast<ObjectHeader *>(static_cast<uint8_t *>(objPtr) - OBJECT_HEADER_SIZE);

        if (header->forwarded()) {
            return header->addr(); // 已复制，直接返回新地址
        }

        size_t payloadSize = header->size() - OBJECT_HEADER_SIZE;
        header->age++;

        void *newDataPtr = nullptr;
        if (header->age >= promotionAgeThreshold) {
            // 晋升到老年代
            newDataPtr = oldGen.alloc(payloadSize, alignof(std::max_align_t));
        } else {
            // 复制到 Survivor To
            newDataPtr = survivorTo.alloc(payloadSize, alignof(std::max_align_t));
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

class GlobalGC {
  public:
    static GarbageCollector &instance() {
        static GarbageCollector gc(GCConfig{
            .edenSize = 16 * 1024 * 1024,    // 16 MB
            .survivorSize = 2 * 1024 * 1024, // 2 MB
            .oldGenSize = 64 * 1024 * 1024,  // 64 MB
            .permGenSize = 16 * 1024 * 1024, // 16 MB
            .promotionAgeThreshold = 2       // 晋升阈值
        });
        return gc;
    }

  private:
    GlobalGC() = default; // 禁止外部构造
};
