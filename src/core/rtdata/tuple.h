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
 * Created: Nov. 12, 2025
 * Updated: Dec. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class GCTuple : public GCObject {
  public:
    GCTuple(const GCTuple &)            = delete;
    GCTuple &operator=(const GCTuple &) = delete;

    /// 创建 GCTuple 实例
    static GCTuple *
    create(const TupleTypeLayout &layout, size_t size, IAllocator &allocator = mm::autoSpace()) {
        size_t headerSize = offsetof(GCTuple, data_);
        size_t dataSize   = sizeof(slot_t) * size;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(GCTuple));
        if (!memory)
            throw std::bad_alloc();

        GCTuple *tuple = new (memory) GCTuple(layout.elemTypes().data(), size);

        // 初始化引用类型为空以避免伪引用
        GCRef *dataStart = reinterpret_cast<GCRef *>(tuple->data_);
        std::fill(dataStart, dataStart + size, NullRef);

        return tuple;
    }

    size_t size() const { return size_; }
    const TypeCode *types() const { return types_; }

    TypeCode elemType(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return types_[index];
    }

    template <typename T> T &at(size_t index) {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<T *>(data_)[index];
    }

    template <typename T> const T &at(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<const T *>(data_)[index];
    }

    slot_t *data() { return reinterpret_cast<slot_t *>(data_); }
    const slot_t *data() const { return reinterpret_cast<const slot_t *>(data_); }

    void onMoved() override {
        // types_ 指向的数组由外部管理，不需要更新
        // data_ 是灵活数组成员，也在对象内部
    }

    void updateRefs(const std::function<GCRef(GCRef)> &relocate) override {
        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types_[i])) {
                GCRef *refArr = reinterpret_cast<GCRef *>(data_);
                for (size_t i = 0; i < size_; ++i) {
                    if (GCRef &ref = refArr[i]) {
                        ref = relocate(ref);
                    }
                }
            }
        }
    }

  private:
    // 注意：types_ 指针指向外部共享数组，调用方必须保证在 tuple 生命周期内有效
    GCTuple(const TypeCode *types, size_t size)
        : size_(static_cast<uint32_t>(size)), types_(types) {}

    uint32_t size_;
    const TypeCode *types_;            // 外部维护的类型数组的指针
    alignas(slot_t) std::byte data_[]; // 灵活数组成员
};
