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
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class Struct : public Object {
  public:
    Struct(const Struct &)            = delete;
    Struct &operator=(const Struct &) = delete;

    static Struct *create(const StructTypeLayout &layout, IAllocator &allocator) {
        size_t fieldCount = layout.fieldCount();
        size_t headerSize = sizeof(Struct);
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

    bool has(std::string_view name) const { return layout_->findField(name).has_value(); }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return fromSlot<T>(data_[index]);
    }

    template <typename T> T get(std::string_view name) const {
        auto optIndex = layout_->findField(name);
        ASSERT(optIndex.has_value(), std::format("Field name not found: {}", name));
        return get<T>(optIndex.value());
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, std::format("Index out of range: {}", index));
        if constexpr (std::is_same_v<T, Object *>) {
            // writeBarrier(arr[index], value);
        }
        data_[index] = toSlot(value);
    }

    template <typename T> void set(std::string_view name, T value) {
        auto optIndex = layout_->findField(name);
        ASSERT(optIndex.has_value(), std::format("Field name not found: {}", name));
        set<T>(optIndex.value(), value);
    }

    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }
    const StructTypeLayout &layout() const { return *layout_; }
    void updateLayout(const StructTypeLayout *layout) { layout_ = layout; }
    TypeCode typeAt(size_t index) const { return layout_->fieldType(index); }

    virtual bool equals(const Object *other, bool deep = false) const override {
        if (!isOfSameCls(this, other))
            return false;

        const Struct *otherStruct = reinterpret_cast<const Struct *>(other);

        // 布局或字段数量不同则不相等
        if (&layout_ != &otherStruct->layout_ || size_ != otherStruct->size_)
            return false;

        const auto &types   = layout_->fieldTypes();
        const slot_t *dataA = data_;
        const slot_t *dataB = otherStruct->data_;

        if (!deep) {
            // 浅比较：直接比较整个内存块（引用字段只比较指针）
            return std::memcmp(dataA, dataB, size_ * sizeof(slot_t)) == 0;
        }

        // 深比较：逐字段递归比较
        for (size_t i = 0; i < size_; ++i) {
            TypeCode type = types[i];
            if (isGCTraced(type)) {
                const Object *refA = reinterpret_cast<const Object *const *>(dataA)[i];
                const Object *refB = reinterpret_cast<const Object *const *>(dataB)[i];
                if (refA == refB)
                    continue;
                if (!refA->equals(refB, true))
                    return false;
            } else {
                if (dataA[i] != dataB[i])
                    return false;
            }
        }

        return true;
    }

    virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
        Struct *newStruct = Struct::create(*layout_, allocator);

        const auto &types = layout_->fieldTypes();
        const slot_t *src = data_;
        slot_t *dst       = newStruct->data_;

        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                const Object *oriRef = reinterpret_cast<const Object *const *>(src)[i];
                Object *newRef       = NullRef;

                if (oriRef) {
                    if (deep) {
                        // 递归克隆引用对象
                        newRef = oriRef->clone(allocator, true);
                    } else {
                        // 浅拷贝：直接引用原指针
                        newRef = const_cast<Object *>(oriRef);
                    }
                }

                reinterpret_cast<Object **>(dst)[i] = newRef;
            } else {
                // 非引用类型：直接复制 slot 数据
                dst[i] = src[i];
            }
        }

        return reinterpret_cast<Object *>(newStruct);
    }

    virtual void print(std::ostream &os) const override {
        if (size_ == 0) {
            os << "{}";
            return;
        }

        os << "{ ";

        const auto &names     = layout_->fieldNames();
        const auto &types     = layout_->fieldTypes();
        const slot_t *dataPtr = data_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";

            os << names[i] << ": ";
            printSlot(os, dataPtr[i], types[i]);
        }

        os << " }";
    }

    virtual void onMoved() override {
        // types_ 指向外部的布局元信息，不需调整
    }

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        Object **refArr   = reinterpret_cast<Object **>(data_);
        const auto &types = layout_->fieldTypes();
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
        : size_(static_cast<uint32_t>(fieldCount)), layout_(&layout) {}

    uint32_t size_;
    const StructTypeLayout *layout_;
    slot_t data_[]; // 灵活数组成员
};
