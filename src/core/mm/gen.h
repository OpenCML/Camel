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
 * Updated: Nov. 22, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/large_obj.h"
#include "gc.h"

#include "utils/assert.h"
#include "utils/brpred.h"

class GenerationalAllocatorWithGC : public IAllocator {
  private:
    enum AllocRegion {
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

    bool inGC_ = false;                                // GC 状态标志
    std::vector<GCObject *> rootObjectSet_;            // 根对象集合
    std::unordered_set<ObjectHeader *> rememberedSet_; // 记忆集

    bool inYoungGen(ObjectHeader *header) const { return header->region_ == AllocRegion::YoungGen; }

    bool inOldGen(ObjectHeader *header) const { return header->region_ == AllocRegion::OldGen; }

    bool inLargeObjSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::LargeObject;
    }

  public:
    struct Config {
        size_t edenSize;
        size_t survivorSize;
        size_t oldGenSize;
        size_t promotionAgeThreshold;
    };

    GenerationalAllocatorWithGC(const Config &config)
        : eden_(config.edenSize), from_(config.survivorSize), to_(config.survivorSize),
          oldGen_(config.oldGenSize), largeObjSpace_(),
          promotionAgeThreshold_(config.promotionAgeThreshold) {}

    void *alloc(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        align = adjustAlign(align);

        // 大对象直接走大对象分配器
        if (UNLIKELY(payloadSize > 1024)) {
            void *ptr = largeObjSpace_.alloc(payloadSize, align);
            if (UNLIKELY(!ptr)) {
                majorGC();
                ptr = largeObjSpace_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
            auto *header = headerOf(ptr);
            header->setRegion(AllocRegion::LargeObject);
            return ptr;
        }

        // 尝试在 Eden 分配
        void *ptr = eden_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            minorGC();
            ptr = eden_.alloc(payloadSize, align);
            if (UNLIKELY(!ptr)) {
                majorGC();
                ptr = eden_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
        }

        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::YoungGen);
        return ptr;
    }

    void free(void *ptr) {
        ASSERT(false, "GenerationalAllocatorWithGC does not support manual free");
    }

    // 添加根对象
    void addRoot(GCObject *root) { rootObjectSet_.push_back(root); }

    // 移除根对象
    void removeRoot(GCObject *root) {
        auto it = std::find(rootObjectSet_.begin(), rootObjectSet_.end(), root);
        if (it != rootObjectSet_.end()) {
            rootObjectSet_.erase(it);
        }
    }

    // 清空所有根对象
    void clearRoots() { rootObjectSet_.clear(); }

    void recordOldToYoungRef(void *oldObj, void *youngObj) {
        ObjectHeader *header = headerOf(oldObj);
        rememberedSet_.insert(header);
    }

    // Minor GC：清理年轻代（Eden + From）
    void minorGC() {
        if (inGC_)
            return; // 防止重入
        inGC_ = true;

        try {
            // 1. 交换 From 和 To 空间
            std::swap(from_, to_);
            to_.reset(); // 清空新的 To 空间

            // 2. 处理根集合中的年轻代对象
            for (GCObject *&rootObj : rootObjectSet_) {
                if (!rootObj)
                    continue;

                ObjectHeader *header = headerOf(rootObj);

                // 只处理年轻代对象
                if (inYoungGen(header)) {
                    rootObj = copyOrPromote(rootObj);
                }
            }

            // 3. 扫描老年代中指向年轻代的引用（记忆集优化）
            for (ObjectHeader *oldHeader : rememberedSet_) {
                if (!oldHeader->isValid())
                    continue;

                GCObject *oldObj = payloadOf<GCObject>(oldHeader);

                // 遍历老年代对象的引用
                oldObj->traverse([this](GCObject *refObj) -> GCObject * {
                    if (!refObj)
                        return nullptr;

                    ObjectHeader *refHeader = headerOf(refObj);

                    // 如果引用的是年轻代对象，需要复制
                    if (inYoungGen(refHeader)) {
                        return copyOrPromote(refObj);
                    }

                    return refObj;
                });
            }

            // 清空记忆集（因为年轻代已被清空）
            rememberedSet_.clear();

            // 4. 使用 Cheney 算法扫描 To 空间
            cheneyScavenge();

            // 5. 清空 Eden 和 From
            eden_.reset();
            from_.reset();

            inGC_ = false;
        } catch (...) {
            inGC_ = false;
            throw;
        }
    }

    // Major GC：清理整个堆
    void majorGC() {
        // 1. 标记阶段：标记所有可达对象
        markPhase();

        // 2. 清理年轻代
        minorGC();

        // 3. 压缩老年代（可选，这里使用标记-清除）
        sweepOldGen();

        // 4. 清理大对象空间
        sweepLargeObjects();
    }

  private:
    GCObject *copyOrPromote(GCObject *obj) {
        ObjectHeader *header = headerOf(obj);
        ASSERT(header->isValid(), "Invalid ObjectHeader encountered during copyOrPromote");

        // 如果已经转发过，直接返回新地址
        if (header->forwarded()) {
            return static_cast<GCObject *>(header->forwardedAddr());
        }

        size_t totalSize = header->size();
        size_t objSize   = header->objSize();

        // 增加年龄
        header->incAge();
        uint64_t age = header->age();

        void *newObj            = nullptr;
        ObjectHeader *newHeader = nullptr;

        // 判断是否晋升
        if (UNLIKELY(age >= promotionAgeThreshold_)) {
            // 晋升到老年代
            newObj = oldGen_.alloc(objSize, alignof(std::max_align_t));
            if (!newObj) {
                // 老年代空间不足，触发 Full GC
                majorGC();
                newObj = oldGen_.alloc(objSize, alignof(std::max_align_t));
                if (!newObj)
                    throw std::bad_alloc();
            }

            newHeader = headerOf(newObj);
            newHeader->setAge(age);
            newHeader->setRegion(AllocRegion::OldGen);
        } else {
            // 复制到 Survivor To
            newObj = to_.alloc(objSize, alignof(std::max_align_t));
            if (UNLIKELY(!newObj)) {
                // To 空间不足，直接晋升
                newObj = oldGen_.alloc(objSize, alignof(std::max_align_t));
                if (UNLIKELY(!newObj)) {
                    if (inGC_) {
                        // 已经在 GC 中，无法再次触发
                        throw std::bad_alloc();
                    }

                    // 标记 GC 状态
                    inGC_ = true;
                    try {
                        majorGC();
                        newObj = oldGen_.alloc(objSize, alignof(std::max_align_t));
                        inGC_  = false;
                    } catch (...) {
                        inGC_ = false;
                        throw;
                    }

                    if (!newObj)
                        throw std::bad_alloc();
                }

                newHeader = headerOf(newObj);
                newHeader->setAge(age);
                newHeader->setRegion(AllocRegion::OldGen);
            } else {
                newHeader = headerOf(newObj);
                newHeader->setAge(age);
                newHeader->setRegion(AllocRegion::YoungGen);
            }
        }

        // 复制 payload 数据
        std::memcpy(newObj, obj, objSize);

        // 设置转发地址
        header->forward(newObj);

        return static_cast<GCObject *>(newObj);
    }

    // Cheney 算法：使用 BFS 方式扫描和复制对象
    void cheneyScavenge() {
        uint8_t *scan = to_.start(); // 扫描指针，从 To 空间开始
        uint8_t *free = to_.top();   // 空闲指针，指向下一个可分配位置

        // BFS 遍历：scan 追赶 free
        while (scan < free) {
            ObjectHeader *header = reinterpret_cast<ObjectHeader *>(scan);
            void *payload        = scan + sizeof(ObjectHeader);

            // 获取实际对象
            GCObject *obj = reinterpret_cast<GCObject *>(payload);

            // 遍历对象的所有引用字段，并转发它们
            obj->traverse([this](GCObject *refObj) -> GCObject * {
                if (!refObj)
                    return nullptr;

                ObjectHeader *refHeader = headerOf(refObj);

                // 老年代对象不需要移动
                if (!inYoungGen(refHeader)) {
                    return refObj;
                }

                // 复制或获取转发地址
                return copyOrPromote(refObj);
            });

            // 移动到下一个对象
            scan += header->size();
            // 更新 free 指针（可能在 traverse 中有新对象被复制）
            free = to_.top();
        }
    }

    // 标记阶段：深度优先标记所有可达对象
    void markPhase() {
        // 清除所有标记
        clearMarks();

        // 从根集合开始标记
        for (GCObject *root : rootObjectSet_) {
            if (root) {
                markObject(root);
            }
        }
    }

    void clearMarks() {
        // 清除老年代标记
        oldGen_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

        // 清除大对象空间标记
        largeObjSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    }

    void markObject(GCObject *obj) {
        if (!obj)
            return;

        std::vector<GCObject *> markStack;
        markStack.push_back(obj);

        while (!markStack.empty()) {
            GCObject *current = markStack.back();
            markStack.pop_back();

            if (!current)
                continue;

            void *payload        = reinterpret_cast<void *>(current);
            ObjectHeader *header = headerOf(payload);

            // 如果已标记，跳过
            if (header->marked_)
                continue;

            // 标记当前对象
            header->mark();

            // 收集所有引用的对象到栈中
            current->traverse([&markStack](GCObject *refObj) -> GCObject * {
                if (refObj) {
                    markStack.push_back(refObj);
                }
                return refObj;
            });
        }
    }

    // 清除老年代中未标记的对象
    void sweepOldGen() {
        std::vector<ObjectHeader *> unreachable;

        oldGen_.iterateAllocated([&unreachable](ObjectHeader *header) {
            if (!header->marked_) {
                unreachable.push_back(header);
            }
        });

        // 批量释放
        oldGen_.freeBulk(unreachable);
    }

    // 清除大对象空间中未标记的对象
    void sweepLargeObjects() {
        std::vector<ObjectHeader *> unreachable;

        largeObjSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
            if (!header->marked_) {
                unreachable.push_back(header);
            }
        });

        // 批量释放
        largeObjSpace_.freeBulk(unreachable);
    }
};
