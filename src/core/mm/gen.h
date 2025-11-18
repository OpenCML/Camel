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
    enum RegionId {
        YoungGen    = 0,
        OldGen      = 1,
        LargeObject = 2,
    };

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

    bool inYoungGen(ObjectHeader *header) const { return header->regionId() == RegionId::YoungGen; }

    bool inOldGen(ObjectHeader *header) const { return header->regionId() == RegionId::OldGen; }

    bool inLargeObjSpace(ObjectHeader *header) const {
        return header->regionId() == RegionId::LargeObject;
    }

  public:
    struct Config {
        size_t edenSize;
        size_t survivorSize;
        size_t oldGenSize;
        size_t promotionAgeThreshold;
    };

    GenerationalAllocatorUsingGC(const Config &config)
        : eden_(config.edenSize), from_(config.survivorSize), to_(config.survivorSize),
          oldGen_(config.oldGenSize), largeObjSpace_(),
          promotionAgeThreshold_(config.promotionAgeThreshold) {}

    void *alloc(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        align = adjustAlign(align);

        // 大对象直接走大对象分配器
        if (__builtin_expect(payloadSize > 1024, 0)) {
            void *ptr = largeObjSpace_.alloc(payloadSize, align);
            if (__builtin_expect(!ptr, 0)) {
                majorGC();
                ptr = largeObjSpace_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
            auto *header = headerOf(ptr);
            header->setRegionId(RegionId::LargeObject);
            return ptr;
        }

        // 尝试在 Eden 分配
        void *ptr = eden_.alloc(payloadSize, align);
        if (__builtin_expect(!ptr, 0)) {
            minorGC();
            ptr = eden_.alloc(payloadSize, align);
            if (__builtin_expect(!ptr, 0)) {
                majorGC();
                ptr = eden_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
        }

        auto *header = headerOf(ptr);
        header->setRegionId(RegionId::YoungGen);
        return ptr;
    }

    void free(void *ptr) {
        ASSERT(false, "GenerationalAllocatorUsingGC does not support manual free");
    }

    GarbageCollector &gc() { return gc_; }

    // Minor GC：清理年轻代（Eden + From）
    void minorGC() {
        // 1. 清空 To 空间
        to_.reset();

        // 2. 第一遍遍历：复制所有可达的年轻代对象
        gc_.traverse([this](ObjectHeader *header) {
            if (inYoungGen(header) && !header->forwarded()) {
                void *oldPayload = payloadOf<void>(header);
                copyOrPromote(oldPayload, header);
            }
        });

        // 3. 第二遍遍历：更新所有对象的内部引用
        //    遍历根对象和它们可达的所有对象
        std::unordered_set<GCObject *> visited;

        auto updateVisitor = [this, &visited](this auto &&self, GCObject *obj) -> void {
            if (!obj || visited.count(obj))
                return;
            visited.insert(obj);

            // 更新这个对象内部的引用
            obj->traverse([&](GCObject *refObj) {
                if (!refObj)
                    return;

                void *refPayload = refObj->payload();
                auto *refHeader  = headerOf(refPayload);

                // 如果被引用的对象被移动了（转发了），通知引用者更新
                if (refHeader->forwarded()) {
                    void *newPayload = refHeader->addr();
                    refObj->onMoved(newPayload);
                }

                // 递归处理被引用的对象
                self(refObj);
            });
        };

        for (auto *root : gc_.rootSet_) {
            root->traverse(updateVisitor);
        }

        // 4. 交换 From 和 To
        std::swap(from_, to_);

        // 5. 清空 Eden
        eden_.reset();
    }

    // Major GC：清理整个堆
    void majorGC() {
        // 1. 标记阶段：标记所有可达对象
        gc_.traverse([](ObjectHeader *header) { header->mark(); });

        // 2. 清理老年代中未标记的对象
        oldGen_.sweep([](ObjectHeader *header) { return header->marked == 0; });

        // 3. 清理大对象空间中未标记的对象
        largeObjSpace_.sweep([](ObjectHeader *header) { return header->marked == 0; });

        // 4. 清理标记位
        gc_.traverse([](ObjectHeader *header) { header->marked = 0; });

        // 5. 同时清理年轻代
        minorGC();
    }

  private:
    void *copyOrPromote(void *objPayload, ObjectHeader *header) {
        if (!objPayload)
            return nullptr;

        // 如果已经转发过，直接返回新地址
        if (header->forwarded()) {
            return header->addr();
        }

        size_t totalSize   = header->size();
        size_t payloadSize = totalSize - sizeof(ObjectHeader);

        // 增加年龄
        uint64_t newAge = header->age + 1;

        void *newPayload        = nullptr;
        ObjectHeader *newHeader = nullptr;

        // 判断是否晋升
        if (newAge >= promotionAgeThreshold_) {
            // 晋升到老年代
            newPayload = oldGen_.alloc(payloadSize, alignof(std::max_align_t));
            if (!newPayload) {
                // 老年代空间不足，触发 Full GC
                majorGC();
                newPayload = oldGen_.alloc(payloadSize, alignof(std::max_align_t));
                if (!newPayload)
                    throw std::bad_alloc();
            }

            newHeader        = headerOf(newPayload);
            newHeader->raw   = header->raw;
            newHeader->age   = newAge;
            newHeader->state = 0;
            newHeader->setRegionId(RegionId::OldGen);

        } else {
            // 复制到 Survivor To
            newPayload = to_.alloc(payloadSize, alignof(std::max_align_t));
            if (!newPayload) {
                // To 空间不足，直接晋升
                newPayload = oldGen_.alloc(payloadSize, alignof(std::max_align_t));
                if (!newPayload) {
                    majorGC();
                    newPayload = oldGen_.alloc(payloadSize, alignof(std::max_align_t));
                    if (!newPayload)
                        throw std::bad_alloc();
                }

                newHeader        = headerOf(newPayload);
                newHeader->raw   = header->raw;
                newHeader->age   = newAge;
                newHeader->state = 0;
                newHeader->setRegionId(RegionId::OldGen);
            } else {
                newHeader        = headerOf(newPayload);
                newHeader->raw   = header->raw;
                newHeader->age   = newAge;
                newHeader->state = 0;
                newHeader->setRegionId(RegionId::YoungGen);
            }
        }

        // 复制 payload 数据
        std::memcpy(newPayload, objPayload, payloadSize);

        // 设置转发地址
        header->forward(newPayload);

        return newPayload;
    }
};
