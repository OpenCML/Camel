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
 * Updated: Nov. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/large_obj.h"
#include "utils/assert.h"

class GCObject {
  public:
    virtual ~GCObject()                                                    = default;
    virtual ObjectHeader *header() const                                   = 0;
    virtual void trace(const std::function<void(GCObject *)> &visit) const = 0;
};

struct GCConfig {
    size_t edenSize;              // Eden 空间大小
    size_t survivorSize;          // 单个 Survivor 区大小
    size_t oldGenSize;            // 老年代大小
    size_t permGenSize;           // 永久代大小
    size_t metaSpaceSize;         // 元数据区大小
    size_t promotionAgeThreshold; // 晋升阈值
};

class GarbageCollector : public IAllocator {
  private:
    // 年轻代
    BumpPointerAllocator eden_;
    BumpPointerAllocator from_;
    BumpPointerAllocator to_;

    // 老年代
    FreeListAllocator oldGen_;

    // 持久代
    BumpPointerAllocator permGen_;

    // 元数据区
    FreeListAllocator metaSpace_;

    // 大对象专用分配器
    LargeObjectAllocator largeObjSpace_;

    size_t promotionAgeThreshold_ = 2;
    std::vector<void *> rootSet_; // 模拟根集合

  public:
    GarbageCollector(const GCConfig &config)
        : eden_(config.edenSize), from_(config.survivorSize), to_(config.survivorSize),
          oldGen_(config.oldGenSize), permGen_(config.permGenSize),
          metaSpace_(config.metaSpaceSize), largeObjSpace_(),
          promotionAgeThreshold_(config.promotionAgeThreshold) {}

    // 添加根对象（模拟）
    void addRoot(void *obj) { rootSet_.push_back(obj); }

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
        ASSERT(false, "GarbageCollector does not support manual free");
    }

    // 元数据区分配
    void *allocMeta(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        void *ptr = metaSpace_.alloc(payloadSize, align);
        if (!ptr)
            throw std::bad_alloc();
        return ptr;
    }

    void freeMeta(void *ptr) { metaSpace_.free(ptr); }

    // 持久代分配
    void *allocPerm(size_t payloadSize, size_t align = alignof(std::max_align_t)) {
        void *ptr = permGen_.alloc(payloadSize, align);
        if (!ptr)
            throw std::bad_alloc();
        return ptr;
    }

    // Minor GC：只清理 Eden + Survivor From
    void minorGC() {
        to_.reset();

        // 模拟 root 集合扫描
        for (void *&rootObj : rootSet_) {
            rootObj = copyOrPromote(rootObj); // 更新根指针
        }

        // 交换 Survivor From / To
        std::swap(from_, to_);

        // 清空 Eden
        eden_.reset();
    }

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

class GlobalGC {
  public:
    static GarbageCollector &instance() {
        static GarbageCollector gc(GCConfig{
            .edenSize              = 1024 * 1024 * 16, // 16 MB
            .survivorSize          = 1024 * 1024 * 2,  // 2 MB
            .oldGenSize            = 1024 * 1024 * 64, // 64 MB
            .permGenSize           = 1024 * 1024 * 16, // 16 MB
            .promotionAgeThreshold = 2                 // 晋升阈值
        });
        return gc;
    }

  private:
    GlobalGC() = default; // 禁止外部构造
};

template <typename T> class GCAllocator {
  public:
    using value_type = T;

    GCAllocator() noexcept {}
    template <class U> GCAllocator(const GCAllocator<U> &) noexcept {}

    // GC 分配内存
    T *allocate(std::size_t n) {
        void *ptr = GlobalGC::instance().allocate(n * sizeof(T), alignof(T));
        if (!ptr)
            throw std::bad_alloc();
        return static_cast<T *>(ptr);
    }

    // GC 管理生命周期，这里不释放
    void deallocate(T *p, std::size_t) noexcept {
        // 不做释放，GC 负责
    }

    // 所有内存都是从同一个全局 GC 池里分配的，没有分配器实例的区别，所以它们天然等价
    template <class U> bool operator==(const GCAllocator<U> &) const noexcept { return true; }
    template <class U> bool operator!=(const GCAllocator<U> &) const noexcept { return false; }
};
