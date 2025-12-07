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
 * Updated: Dec. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class GCStruct : public GCObject {
  public:
    GCStruct(const GCStruct &)            = delete;
    GCStruct &operator=(const GCStruct &) = delete;

    static GCStruct *
    create(const StructTypeLayout &layout, IAllocator &allocator = mm::autoSpace()) {
        size_t fieldCount = layout.fieldCount();
        size_t headerSize = offsetof(GCStruct, data_);
        size_t dataSize   = sizeof(slot_t) * fieldCount;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(GCStruct));
        if (!memory)
            throw std::bad_alloc();

        GCStruct *s = new (memory) GCStruct(layout, fieldCount);

        // 初始化所有引用类型的字段为 NullRef
        GCRef *dataStart = reinterpret_cast<GCRef *>(s->data_);
        std::fill(dataStart, dataStart + fieldCount, NullRef);

        return s;
    }

    size_t size() const { return size_; }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<const T *>(data_)[index];
    }

    template <typename T> T get(std::string_view name) const {
        auto optIndex = layout_.findField(name);
        ASSERT(optIndex.has_value(), "Field name not found: {}", name);
        return get<T>(optIndex.value());
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");

        T *arr = reinterpret_cast<T *>(data_);
        if constexpr (std::is_same_v<T, GCRef>) {
            // writeBarrier(arr[index], value);
        }
        arr[index] = value;
    }

    template <typename T> void set(std::string_view name, T value) {
        auto optIndex = layout_.findField(name);
        ASSERT(optIndex.has_value(), "Field name not found: {}", name);
        set<T>(optIndex.value(), value);
    }

    std::optional<size_t> findField(std::string_view name, const StructTypeLayout &layout) const {
        return layout.findField(name);
    }

    slot_t *data() { return reinterpret_cast<slot_t *>(data_); }
    const slot_t *data() const { return reinterpret_cast<const slot_t *>(data_); }

    void onMoved() override {
        // types_ 指向外部的布局元信息，不需调整
    }

    void updateRefs(const std::function<GCRef(GCRef)> &relocate) override {
        GCRef *refArr     = reinterpret_cast<GCRef *>(data_);
        const auto &types = layout_.fieldTypes();
        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                if (GCRef &ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    GCStruct(const StructTypeLayout &layout, size_t fieldCount)
        : size_(static_cast<uint32_t>(fieldCount)), layout_(layout) {}

    uint32_t size_;
    const StructTypeLayout &layout_;
    alignas(slot_t) std::byte data_[]; // 灵活数组成员
};
