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
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/large_obj.h"
#include "core/rtdata/base.h"

#include "utils/assert.h"
#include "utils/brpred.h"

// ============================================================================
// 分代GC内存布局总览
// ============================================================================
//
//  ┌────────────────────────────────────────────────────┐
//  │                 Young Generation                   │  ← 新对象诞生地，采用复制收集
//  ├────────────────────────────────────────────────────┤
//  │  ┌──────────────────────────────────────────────┐  │
//  │  │  birthSpace_ (Birth Space / Eden)            │  │  ← 新生对象快速分配区
//  │  │  ████████████████████████████████████████    │  │     [Minor GC 源]
//  │  │  (Larger, e.g., 8MB)                         │  │
//  │  └──────────────────────────────────────────────┘  │
//  │                                                    │
//  │  ┌────────────────────────────┐                    │
//  │  │ havenSpace_ (To Space)     │                    │  ← 幸存者区 To
//  │  │  ████████████████          │                    │     [Minor GC 目标]
//  │  │  (Smaller, e.g., 1MB)      │                    │
//  │  └────────────────────────────┘                    │
//  │           ⇅ (Swap during Minor GC)                │
//  │  ┌────────────────────────────┐                    │
//  │  │ cacheSpace_ (From Space)   │                    │  ← 幸存者区 From
//  │  │  ████████████████          │                    │     [Minor GC 源]
//  │  │  (Equal to havenSpace_)    │                    │
//  │  └────────────────────────────┘                    │
//  └────────────────────────────────────────────────────┘
//           │ (age >= threshold)
//           │ 晋升 (Promotion)
//           ↓
//  ┌────────────────────────────────────┐
//  │          Elder Generation          │  ← 长寿对象存储区
//  ├────────────────────────────────────┤
//  │  elderGenSpace_                    │  ← FreeList 分配器
//  │  ┌─────┐ ┌────┐ ┌──────┐ ┌─────┐   │     [标记-清除算法]
//  │  │ Obj │ │Free│ │ Obj  │ │ Obj │   │
//  │  └─────┘ └────┘ └──────┘ └─────┘   │     碎片化管理
//  │  ████████████████████████████████  │     (Free List 链表)
//  │  (Dynamic sizing)                  │
//  └────────────────────────────────────┘
//
//  ┌────────────────────────────────────┐
//  │         Large Object Space         │  ← 超大对象专用区
//  ├────────────────────────────────────┤
//  │  largeObjSpace_                    │  ← 独立分配器
//  │  ┌────────────────────────────┐    │     [避免复制开销]
//  │  │   Large Object 1           │    │
//  │  │   (size > threshold)       │    │
//  │  └────────────────────────────┘    │
//  │  ┌────────────────────────────┐    │
//  │  │   Large Object 2           │    │
//  │  └────────────────────────────┘    │
//  │  ████████████████████████████████  │
//  │  (Dynamic sizing)                  │
//  └────────────────────────────────────┘
//
// ============================================================================
// 新生代各子空间大小对比（典型配置）
// ============================================================================
//
//    birthSpace_:    ████████████████████████ (8MB)              ← Birth区最大
//    havenSpace_:    ███ (1MB)                                   ← Haven较小
//    cacheSpace_:    ███ (1MB)                                   ← 与Haven相同
//    elderGenSpace_: ██████████████████████████████████████████████████ (16MB)
//
//  设计理念：
//  - birthSpace_ 大：大部分对象"朝生夕死"，需要足够空间容纳大量短命对象，以减少GC频率
//  - havenSpace/cacheSpace 小：每次GC只有少量对象（一般不足10%）存活，不需要太大空间
//  - 典型比例：Birth : Haven : Haven = 8 : 1 : 1 (可调)
//
// ============================================================================

// ============================================================================
// 对象生命周期流程图
// ============================================================================
//
//   [new Object]
//        │
//        ├─→ (size > largeObjThreshold_) ──→ largeObjSpace_
//        │                                      │
//        │                                      ↓
//        │                                  [Major GC 清理]
//        │
//        └─→ (小对象) ──→ birthSpace_
//                           │
//                           ↓
//                      [Minor GC]
//                           │
//              ┌────────────┴────────────┐
//              │                         │
//       (对象存活/可达)           (对象死亡/不可达)
//              │                         │
//              ↓                         ↓
//         forward() (age+=1)        [被自动回收]
//              │
//    ┌─────────┴─────────┐
//    │                   │
// (age < threshold)  (age >= threshold)
//    │                   │
//    ↓                   ↓
// havenSpace_      elderGenSpace_
//  (继续在年轻代)    (晋升到老年代)
//    │                   │
//    └────────┬──────────┘
//             ↓
//        [Major GC 清理]
//
// ============================================================================

// ============================================================================
// Minor GC 详细流程
// ============================================================================
//
//  GC 前状态：
//  ┌─────────────────┐
//  │  birthSpace_    │  [████████████████████████████] ← 满了，触发 Minor GC
//  └─────────────────┘
//  ┌─────────────────┐
//  │  havenSpace_    │  [███░░░] ← To 空间（有存活对象）
//  └─────────────────┘
//  ┌─────────────────┐
//  │  cacheSpace_    │  [░░░░░░] ← From 空间（空闲）
//  └─────────────────┘
//
//  步骤 1: 交换 havenSpace_ ↔ cacheSpace_
//  ┌─────────────────┐
//  │  birthSpace_    │  [████████████████████████████]
//  └─────────────────┘
//  ┌─────────────────┐
//  │  havenSpace_    │  [░░░░░░] ← 新 To（原 From）
//  └─────────────────┘
//  ┌─────────────────┐
//  │  cacheSpace_    │  [███░░░] ← 新 From（原 To）
//  └─────────────────┘
//
//  步骤 2: 复制存活/可达对象（从根对象集合开始遍历以标记可达对象）
//  ┌─────────────────┐
//  │  birthSpace_    │  [████████████████████████████] → 扫描存活对象
//  └─────────────────┘       ↓
//  ┌─────────────────┐       ↓ (copy move, age+=1)
//  │  havenSpace_    │  [██░░░░] ← 存活对象被复制到这里
//  └─────────────────┘       ↑ (copy move, age+=1)
//  ┌─────────────────┐       ↑
//  │  cacheSpace_    │  [███░░░] → 扫描存活对象
//  └─────────────────┘
//
//  步骤 3: 批量清空 birthSpace_ 和 cacheSpace_
//  ┌─────────────────┐
//  │  birthSpace_    │  [░░░░░░░░░░░░░░░░░░░░░░░░░░░░] ← 已清空
//  └─────────────────┘
//  ┌─────────────────┐
//  │  havenSpace_    │  [██░░░░] ← 只保留存活对象
//  └─────────────────┘
//  ┌─────────────────┐
//  │  cacheSpace_    │  [░░░░░░] ← 已清空
//  └─────────────────┘
//
// ============================================================================

class GenerationalAllocatorWithGC : public IAllocator {
  public:
    struct Config {
        size_t birthSize;
        size_t havenSize;
        size_t elderGenSize;
        size_t promotionAgeThreshold;
        size_t largeObjThreshold;
        float minorGCTriggerRatio;
        float majorGCTriggerRatio;
    };

    GenerationalAllocatorWithGC(const Config &config)
        : birthSpace_(config.birthSize), havenSpace_(config.havenSize),
          cacheSpace_(config.havenSize), elderGenSpace_(config.elderGenSize), largeObjSpace_(),
          promotionAgeThreshold_(config.promotionAgeThreshold),
          largeObjThreshold_(config.largeObjThreshold),
          minorGCTriggerRatio_(config.minorGCTriggerRatio),
          majorGCTriggerRatio_(config.majorGCTriggerRatio) {}

    void *alloc(size_t payloadSize, size_t align = alignof(slot_t)) {
        ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");

        // 大对象直接走大对象分配器
        if (UNLIKELY(payloadSize > largeObjThreshold_)) {
            void *ptr = largeObjSpace_.alloc(payloadSize, align);
            if (UNLIKELY(!ptr)) {
                majorGC();
                ptr = largeObjSpace_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
            auto *header = headerOf(ptr);
            header->setRegion(AllocRegion::LargeObj);
            return ptr;
        }

        // 尝试在 birth 分配
        void *ptr = birthSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            minorGC();
            ptr = birthSpace_.alloc(payloadSize, align);
            if (UNLIKELY(!ptr)) {
                majorGC();
                ptr = birthSpace_.alloc(payloadSize, align);
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
    void addRoot(Object *root) { rootObjectSet_.push_back(root); }

    // 移除根对象
    void removeRoot(Object *root) {
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

    // Minor GC：清理年轻代（Birth + From）
    void minorGC() {
        if (inGC_)
            return; // 防止重入
        inGC_ = true;

        try {
            // 1. 交换 Cache 和 Haven 空间
            std::swap(cacheSpace_, havenSpace_);
            havenSpace_.reset(); // 清空新的 Haven 空间

            // 2. 处理根集合中的年轻代对象
            for (Object *&rootObj : rootObjectSet_) {
                if (!rootObj)
                    continue;

                ObjectHeader *header = headerOf(rootObj);

                // 只处理年轻代对象
                if (inYoungGenSpace(header)) {
                    rootObj = forward(rootObj);
                }
            }

            // 3. 扫描老年代中指向年轻代的引用（记忆集优化）
            for (ObjectHeader *oldHeader : rememberedSet_) {
                if (!oldHeader->isValid())
                    continue;

                Object *oldObj = payloadOf<Object>(oldHeader);

                // 遍历更新老年代对象的引用
                oldObj->updateRefs([this](Object *ref) -> Object * {
                    if (!ref)
                        return nullptr;

                    ObjectHeader *refHeader = headerOf(ref);

                    // 如果引用的是年轻代对象，需要复制
                    if (inYoungGenSpace(refHeader)) {
                        return forward(ref);
                    }

                    return ref;
                });
            }

            // 清空记忆集（因为年轻代已被清空）
            rememberedSet_.clear();

            // 4. 使用 Cheney 算法扫描 To 空间
            cheneyScavenge();

            // 5. 清空 Birth 和 Cache
            birthSpace_.reset();
            cacheSpace_.reset();

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
    // ============================================================================
    // 区域标识枚举
    // ============================================================================
    enum AllocRegion {
        YoungGen, // 年轻代：存放新创建的对象
        ElderGen, // 老年代：存放长期存活的对象
        LargeObj, // 大对象区：存放超过阈值的大对象
    };

    // ============================================================================
    // 年轻代（Young Generation）- 三空间复制收集器
    // ============================================================================
    // 工作机制：采用"三色标记 + 复制收集"策略
    // - birthSpace_：对象诞生地，所有小对象首次在此分配
    // - havenSpace_：当前幸存者空间（To-space），存放上次 GC 幸存的对象
    // - cacheSpace_：备用幸存者空间（From-space），与 havenSpace_ 角色交换
    //
    // Minor GC 流程：
    //   1. 交换 havenSpace_ ↔ cacheSpace_（角色互换）
    //   2. 从 birthSpace_ + cacheSpace_(旧 To) 复制存活对象到 havenSpace_(新 To)
    //   3. 清空 birthSpace_ 和 cacheSpace_
    // ============================================================================
    BumpPointerAllocator birthSpace_;
    BumpPointerAllocator havenSpace_;
    BumpPointerAllocator cacheSpace_;

    // ============================================================================
    // 老年代（Elder Generation）- 标记-清除收集器
    // ============================================================================
    // 作用：存放"晋升"的长寿对象（age >= promotionAgeThreshold_）
    // 特点：使用 FreeList 管理碎片，空间利用率高但分配稍慢
    // GC 策略：Major GC 时执行标记-清除（Mark-Sweep）
    // ============================================================================
    FreeListAllocator elderGenSpace_;

    // ============================================================================
    // 大对象空间（Large Object Space）- 独立管理
    // ============================================================================
    // 作用：直接分配超过 largeObjThreshold_ 的大对象
    // 优势：避免复制开销，直接在独立区域管理
    // GC 策略：Major GC 时执行标记-清除
    // ============================================================================
    LargeObjectAllocator largeObjSpace_;

    // ============================================================================
    // GC 参数配置
    // ============================================================================
    size_t promotionAgeThreshold_; // 晋升年龄阈值：对象经历多少次 Minor GC 后晋升到老年代
    size_t largeObjThreshold_;     // 大对象阈值：超过此大小直接进入大对象空间
    float minorGCTriggerRatio_;    // Minor GC 触发比例（预留，birthSpace_ 满时触发）
    float majorGCTriggerRatio_;    // Major GC 触发比例（老年代空间使用率）

    // ============================================================================
    // GC 状态与根集合
    // ============================================================================
    bool inGC_ = false;                                // GC 重入保护标志
    std::vector<Object *> rootObjectSet_;              // 根对象集合：栈、全局变量等直接可达对象
    std::unordered_set<ObjectHeader *> rememberedSet_; // 记忆集：记录老年代→年轻代的跨代引用

    bool inYoungGenSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::YoungGen;
    }
    bool inElderGenSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::ElderGen;
    }
    bool inLargeObjSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::LargeObj;
    }

    Object *forward(Object *obj) {
        ObjectHeader *header = headerOf(obj);
        ASSERT(header->isValid(), "Invalid ObjectHeader encountered during forwarding");

        // 如果已经转发过，直接返回新地址
        if (header->forwarded()) {
            return static_cast<Object *>(header->forwardedAddr());
        }

        size_t objSize = header->objSize();

        // 增加年龄
        header->incAge();
        uint64_t age = header->age();

        void *newObj            = nullptr;
        ObjectHeader *newHeader = nullptr;

        // 判断是否晋升
        if (UNLIKELY(age >= promotionAgeThreshold_)) {
            // 晋升到老年代
            newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
            if (!newObj) {
                // 老年代空间不足，触发 Full GC
                majorGC();
                newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
                if (!newObj)
                    throw std::bad_alloc();
            }

            newHeader = headerOf(newObj);
            newHeader->setAge(age);
            newHeader->setRegion(AllocRegion::ElderGen);
        } else {
            // 复制到 Survivor To
            newObj = havenSpace_.alloc(objSize, alignof(slot_t));
            if (UNLIKELY(!newObj)) {
                // To 空间不足，直接晋升
                newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
                if (UNLIKELY(!newObj)) {
                    if (inGC_) {
                        // 已经在 GC 中，无法再次触发
                        throw std::bad_alloc();
                    }

                    // 标记 GC 状态
                    inGC_ = true;
                    try {
                        majorGC();
                        newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
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
                newHeader->setRegion(AllocRegion::ElderGen);
            } else {
                newHeader = headerOf(newObj);
                newHeader->setAge(age);
                newHeader->setRegion(AllocRegion::YoungGen);
            }
        }

        // 复制 payload 数据
        std::memcpy(newObj, (void *)obj, objSize);

        // 通知复制后的obj已经被移动
        Object *gcObj = reinterpret_cast<Object *>(newObj);
        gcObj->onMoved();

        // 设置转发地址
        header->forward(newObj);

        return static_cast<Object *>(newObj);
    }

    // Cheney 算法：使用 BFS 方式扫描和复制对象
    void cheneyScavenge() {
        std::byte *scan = havenSpace_.start(); // 扫描指针，从 To 空间开始
        std::byte *free = havenSpace_.top();   // 空闲指针，指向下一个可分配位置

        // BFS 遍历：scan 追赶 free
        while (scan < free) {
            ObjectHeader *header = reinterpret_cast<ObjectHeader *>(scan);
            void *payload        = scan + sizeof(ObjectHeader);

            // 获取实际对象
            Object *ref = reinterpret_cast<Object *>(payload);

            // 遍历对象的所有引用字段，并转发它们
            ref->updateRefs([this](Object *ref) -> Object * {
                if (!ref)
                    return nullptr;

                ObjectHeader *refHeader = headerOf(ref);

                // 老年代对象不需要移动
                if (!inYoungGenSpace(refHeader)) {
                    return ref;
                }

                // 复制或获取转发地址
                return forward(ref);
            });

            // 移动到下一个对象
            scan += header->size();
            // 更新 free 指针（可能在 traverse 中有新对象被复制）
            free = havenSpace_.top();
        }
    }

    // 标记阶段：深度优先标记所有可达对象
    void markPhase() {
        // 清除所有标记
        clearMarks();

        // 从根集合开始标记
        for (Object *root : rootObjectSet_) {
            if (root) {
                markObject(root);
            }
        }
    }

    void clearMarks() {
        // 清除老年代标记
        elderGenSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

        // 清除大对象空间标记
        largeObjSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    }

    void markObject(Object *obj) {
        if (!obj)
            return;

        std::vector<Object *> markStack;
        markStack.push_back(obj);

        while (!markStack.empty()) {
            Object *current = markStack.back();
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
            current->updateRefs([&markStack](Object *ref) -> Object * {
                if (ref) {
                    markStack.push_back(ref);
                }
                return ref;
            });
        }
    }

    // 清除老年代中未标记的对象
    void sweepOldGen() {
        std::vector<ObjectHeader *> unreachable;

        elderGenSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
            if (!header->marked_) {
                unreachable.push_back(header);
            }
        });

        // 批量释放
        elderGenSpace_.freeBulk(unreachable);
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
