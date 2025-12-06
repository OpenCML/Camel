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
 * Updated: Dec. 06, 2025
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

        GCStruct *s = new (memory) GCStruct(layout.fieldTypes().data(), fieldCount);

        // 初始化所有引用类型的字段为 NullRef
        GCRef *dataStart = reinterpret_cast<GCRef *>(s->data_);
        std::fill(dataStart, dataStart + fieldCount, NullRef);

        return s;
    }

    size_t size() const { return fieldCount_; }
    const TypeCode *fieldTypes() const { return fieldTypes_; }

    TypeCode fieldType(size_t index) const {
        ASSERT(index < fieldCount_, "Field index out of range");
        return fieldTypes_[index];
    }

    template <typename T> T &field(size_t index) {
        ASSERT(index < fieldCount_, "Field index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<T *>(data_)[index];
    }

    template <typename T> const T &field(size_t index) const {
        ASSERT(index < fieldCount_, "Field index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<const T *>(data_)[index];
    }

    std::optional<size_t> findField(std::string_view name, const StructTypeLayout &layout) const {
        return layout.findField(name);
    }

    slot_t *data() { return reinterpret_cast<slot_t *>(data_); }
    const slot_t *data() const { return reinterpret_cast<const slot_t *>(data_); }

    void onMoved() override {
        // fieldTypes_ 指向外部的布局元信息，不需调整
    }

    void updateRefs(const std::function<GCRef(GCRef)> &relocate) override {
        GCRef *refArr = reinterpret_cast<GCRef *>(data_);
        for (size_t i = 0; i < fieldCount_; ++i) {
            if (isGCTraced(fieldTypes_[i])) {
                if (GCRef &ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    // 注意：fieldTypes_ 指向的数组由外部（StructLayout）维护，必须保持有效
    GCStruct(const TypeCode *types, size_t fieldCount)
        : fieldCount_(static_cast<uint32_t>(fieldCount)), fieldTypes_(types) {}

    uint32_t fieldCount_;
    const TypeCode *fieldTypes_;       // 指向外部布局信息
    alignas(slot_t) std::byte data_[]; // 灵活数组成员
};
