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
 * Updated: Dec. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class Struct : public Object {
  public:
    Struct(const Struct &)            = delete;
    Struct &operator=(const Struct &) = delete;

    static Struct *create(const StructTypeLayout &layout, IAllocator &allocator = mm::autoSpace()) {
        size_t fieldCount = layout.fieldCount();
        size_t headerSize = offsetof(Struct, data_);
        size_t dataSize   = sizeof(slot_t) * fieldCount;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Struct));
        if (!memory)
            throw std::bad_alloc();

        Struct *s = new (memory) Struct(layout, fieldCount);

        // 初始化所有引用类型的字段为 NullRef
        Object **dataStart = reinterpret_cast<Object **>(s->data_);
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
        if constexpr (std::is_same_v<T, Object *>) {
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

    virtual bool equals(const Object *other, bool deep = false) const override {
        const Struct *rhs = reinterpret_cast<const Struct *>(other);

        if (this == rhs)
            return true;
        if (!isOfSameCls(this, rhs))
            return false;
        if (&layout_ != &rhs->layout_ || size_ != rhs->size_)
            return false;

        const auto &types = layout_.fieldTypes();

        const slot_t *lhsData = reinterpret_cast<const slot_t *>(data_);
        const slot_t *rhsData = reinterpret_cast<const slot_t *>(rhs->data_);

        for (size_t i = 0; i < size_; ++i) {
            TypeCode type = types[i];
            if (isGCTraced(type)) {
                Object *lhsRef = reinterpret_cast<const Object **>(lhsData)[i];
                Object *rhsRef = reinterpret_cast<const Object **>(rhsData)[i];

                if (lhsRef == rhsRef)
                    continue;

                if (!lhsRef || !rhsRef)
                    return false;

                if (deep) {
                    if (!lhsRef->equals(rhsRef, true))
                        return false;
                } else {
                    return false; // 浅比较要求引用相同
                }
            } else {
                if (lhsData[i] != rhsData[i])
                    return false;
            }
        }

        return true;
    }

    virtual Object *clone(IAllocator &allocator, bool deep) const override {
        Struct *newStruct = Struct::create(layout_, allocator);

        const auto &types = layout_.fieldTypes();
        const slot_t *src = reinterpret_cast<const slot_t *>(data_);
        slot_t *dst       = reinterpret_cast<slot_t *>(newStruct->data_);

        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                const Object *ref = reinterpret_cast<const Object **>(src)[i];
                Object *&dstRef   = reinterpret_cast<Object **>(dst)[i];
                if (ref) {
                    dstRef = deep ? reinterpret_cast<Object *>(ref)->clone(allocator, true) : ref;
                } else {
                    dstRef = NullRef;
                }
            } else {
                dst[i] = src[i];
            }
        }

        return newStruct;
    }

    virtual void onMoved() override {
        // types_ 指向外部的布局元信息，不需调整
    }

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        Object **refArr   = reinterpret_cast<Object **>(data_);
        const auto &types = layout_.fieldTypes();
        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                if (Object *&ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    Struct(const StructTypeLayout &layout, size_t fieldCount)
        : size_(static_cast<uint32_t>(fieldCount)), layout_(layout) {}

    uint32_t size_;
    const StructTypeLayout &layout_;
    alignas(slot_t) std::byte data_[]; // 灵活数组成员
};
