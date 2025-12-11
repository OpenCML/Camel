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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class Tuple : public Object {
  public:
    Tuple(const Tuple &)            = delete;
    Tuple &operator=(const Tuple &) = delete;

    /// 创建 Tuple 实例
    static Tuple *create(const TupleTypeLayout &layout, IAllocator &allocator) {
        size_t headerSize = sizeof(Tuple);
        size_t dataSize   = sizeof(slot_t) * layout.size();
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Tuple));
        if (!memory)
            throw std::bad_alloc();

        return new (memory) Tuple(&layout);
    }

    size_t size() const { return size_; }
    const TupleTypeLayout &layout() const { return *layout_; }
    TypeCode typeAt(size_t index) const { return layout_->typeAt(index); }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return fromSlot<T>(data_[index]);
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        if constexpr (std::is_same_v<T, Object *>) {
            // writeBarrier(arr[index], value);
        }
        data_[index] = toSlot(value);
    }

    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }

    virtual bool equals(const Object *other, bool deep = false) const override {
        if (!isOfSameCls(this, other))
            return false;

        const Tuple *otherTuple = reinterpret_cast<const Tuple *>(other);
        if (size_ != otherTuple->size_)
            return false;

        const auto &types   = layout_->elemTypes();
        const slot_t *dataA = data_;
        const slot_t *dataB = otherTuple->data_;

        if (deep) {
            // 深比较：引用类型递归比较，普通类型直接值比较
            for (size_t i = 0; i < size_; ++i) {
                if (isGCTraced(types[i])) {
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
        } else {
            // 浅比较：所有 slot 原样比较（对于引用，仅比较指针地址）
            return std::memcmp(dataA, dataB, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
        Tuple *newTuple   = Tuple::create(*layout_, allocator);
        const auto &types = layout_->elemTypes();

        const slot_t *src = data_;
        slot_t *dst       = newTuple->data_;

        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                const Object *oriRef = reinterpret_cast<const Object *const *>(src)[i];
                Object *newRef       = NullRef;

                if (oriRef) {
                    if (deep) {
                        // 深拷贝：递归克隆引用对象
                        newRef = oriRef->clone(allocator, true);
                    } else {
                        // 浅拷贝：仅复制引用指针
                        newRef = const_cast<Object *>(oriRef);
                    }
                }
                reinterpret_cast<Object **>(dst)[i] = newRef;
            } else {
                // 非引用类型，直接复制 slot 数据
                dst[i] = src[i];
            }
        }

        return reinterpret_cast<Object *>(newTuple);
    }

    virtual void print(std::ostream &os) const override {
        os << "(";

        const auto &types     = layout_->elemTypes();
        const slot_t *dataPtr = data_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            printSlot(os, dataPtr[i], types[i]);
        }

        os << ")";
    }

    virtual void onMoved() override {}

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        const auto &types = layout_->elemTypes();
        Object **refArr   = reinterpret_cast<Object **>(data_);

        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(types[i])) {
                if (Object *&ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    Tuple(const TupleTypeLayout *layout)
        : size_(static_cast<uint32_t>(layout->size())), layout_(layout) {}

    uint32_t size_;
    const TupleTypeLayout *layout_;
    slot_t data_[];
};
