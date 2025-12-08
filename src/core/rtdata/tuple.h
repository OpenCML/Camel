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
 * Updated: Dec. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class Tuple : public Object {
  public:
    Tuple(const Tuple &)            = delete;
    Tuple &operator=(const Tuple &) = delete;

    /// 创建 Tuple 实例
    static Tuple *create(const TupleTypeLayout &layout, IAllocator &allocator = mm::autoSpace()) {
        size_t headerSize = offsetof(Tuple, data_);
        size_t dataSize   = sizeof(slot_t) * layout.size();
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Tuple));
        if (!memory)
            throw std::bad_alloc();

        Tuple *tuple = new (memory) Tuple(&layout);

        // 初始化引用类型为空以避免伪引用
        Object **dataStart = reinterpret_cast<Object **>(tuple->data_);
        std::fill(dataStart, dataStart + layout.size(), NullRef);

        return tuple;
    }

    size_t size() const { return size_; }

    TypeCode typeAt(size_t index) const { return layout_->typeAt(index); }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<const T *>(data_)[index];
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

    slot_t *data() { return reinterpret_cast<slot_t *>(data_); }
    const slot_t *data() const { return reinterpret_cast<const slot_t *>(data_); }

    virtual bool equals(const Object *other, bool deep = false) const override {
        const Tuple *otherTuple = reinterpret_cast<const Tuple *>(other);
        if (this == otherTuple)
            return true;
        if (!isOfSameCls(this, otherTuple))
            return false;
        if (size_ != otherTuple->size_)
            return false;

        const auto &types = layout_->elemTypes();
        if (deep) {
            for (size_t i = 0; i < size_; ++i) {
                if (isGCTraced(types[i])) {
                    const Object *refA = this->get<Object *>(i);
                    const Object *refB = otherTuple->get<Object *>(i);
                    if (!refA->equals(refB, true))
                        return false;
                } else {
                    slot_t valA = this->get<slot_t>(i);
                    slot_t valB = otherTuple->get<slot_t>(i);
                    if (valA != valB)
                        return false;
                }
            }
        } else {
            return memcmp(this->data(), otherTuple->data(), size_ * sizeof(slot_t)) == 0;
        }

        return true;
    }

    virtual Object *
    clone(IAllocator &allocator = mm::autoSpace(), bool deep = false) const override {
        Tuple *newTuple   = Tuple::create(*layout_, allocator);
        const auto &types = layout_->elemTypes();

        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                Object *originalRef = this->get<Object *>(i);
                if (originalRef) {
                    if (deep) {
                        // 深拷贝：递归克隆引用对象
                        Object *obj       = reinterpret_cast<Object *>(originalRef);
                        Object *clonedRef = obj->clone(allocator, true);
                        newTuple->set<Object *>(i, clonedRef);
                    } else {
                        // 浅拷贝：直接复制引用
                        newTuple->set<Object *>(i, originalRef);
                    }
                } else {
                    newTuple->set<Object *>(i, NullRef);
                }
            } else {
                // 非引用类型，直接复制值
                slot_t value = this->get<slot_t>(i);
                newTuple->set<slot_t>(i, value);
            }
        }

        return reinterpret_cast<Object *>(newTuple);
    }

    virtual void onMoved() override {}

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        const auto &types = layout_->elemTypes();
        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                Object **refArr = reinterpret_cast<Object **>(data_);
                for (size_t i = 0; i < size_; ++i) {
                    if (Object *&ref = refArr[i]) {
                        ref = relocate(ref);
                    }
                }
            }
        }
    }

  private:
    Tuple(const TupleTypeLayout *layout)
        : size_(static_cast<uint32_t>(layout->size())), layout_(layout) {}

    uint32_t size_;
    const TupleTypeLayout *layout_;
    alignas(slot_t) std::byte data_[];
};
