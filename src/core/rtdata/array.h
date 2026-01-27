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
 * Updated: Jan. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

#include <algorithm>

// FixedArray: 固定大小的数组，对象体和数据区在一块连续内存中
class FixedArray : public Object {
  public:
    // 禁止直接构造
    FixedArray(const FixedArray &)            = delete;
    FixedArray &operator=(const FixedArray &) = delete;

    // 静态创建方法（仅需槽个数，layout 通过 updateLayout 延后设置）
    static FixedArray *create(size_t size, IAllocator &allocator) {
        size_t headerSize = sizeof(FixedArray);
        size_t dataSize   = size * sizeof(slot_t);
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(FixedArray));
        if (!memory)
            throw std::bad_alloc();

        FixedArray *array = new (memory) FixedArray(size);
        std::fill(array->data_, array->data_ + size, NullSlot);
        return array;
    }

    size_t size() const { return size_; }
    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }
    const ArrayTypeLayout *layout() const {
        ASSERT(layout_, "FixedArray layout must be set via updateLayout before use");
        return layout_;
    }
    void updateLayout(const ArrayTypeLayout *layout) { layout_ = layout; }

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

    virtual bool equals(const Object *other, bool deep = false) const override {
        ASSERT(layout_, "FixedArray layout must be set via updateLayout before use");
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        TypeCode type = layout_->elemType();

        const FixedArray *otherArray = reinterpret_cast<const FixedArray *>(other);
        if (size_ != otherArray->size_)
            return false;

        const Object *const *arrA = reinterpret_cast<const Object *const *>(data_);
        const Object *const *arrB = reinterpret_cast<const Object *const *>(otherArray->data_);

        if (isGCTraced(type)) {
            if (deep) {
                // 深比较引用对象
                for (size_t i = 0; i < size_; ++i) {
                    const Object *refA = arrA[i];
                    const Object *refB = arrB[i];
                    if (refA == refB)
                        continue;
                    if (!refA->equals(refB, true))
                        return false;
                }
                return true;
            } else {
                // 浅比较引用指针地址
                return std::memcmp(arrA, arrB, size_ * sizeof(Object *)) == 0;
            }
        } else {
            return std::memcmp(arrA, arrB, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
        ASSERT(layout_, "FixedArray layout must be set via updateLayout before use");
        FixedArray *newArray = FixedArray::create(size_, allocator);
        newArray->updateLayout(layout_);
        const Object *const *srcArr = reinterpret_cast<const Object *const *>(data_);
        const Object **dstArr       = reinterpret_cast<const Object **>(newArray->data_);

        TypeCode type = layout_->elemType();

        if (isGCTraced(type)) {
            for (size_t i = 0; i < size_; ++i) {
                const Object *oriRef = srcArr[i];
                if (oriRef) {
                    if (deep) {
                        // 深拷贝：递归克隆引用对象
                        const Object *obj = reinterpret_cast<const Object *>(oriRef);
                        Object *clonedRef = obj->clone(allocator, true);
                        dstArr[i]         = clonedRef;
                    } else {
                        // 浅拷贝：直接复制引用
                        dstArr[i] = oriRef;
                    }
                } else {
                    dstArr[i] = NullRef;
                }
            }
        } else {
            // 非引用类型，直接复制数据
            std::memcpy(dstArr, srcArr, size_ * sizeof(slot_t));
        }

        return reinterpret_cast<Object *>(newArray);
    }

    virtual void print(std::ostream &os) const override {
        ASSERT(layout_, "FixedArray layout must be set via updateLayout before use");
        os << "[";

        TypeCode elemType     = layout_->elemType();
        const slot_t *dataPtr = data_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            printSlot(os, dataPtr[i], elemType);
        }

        os << "]";
    }

    virtual void onMoved() override {
        // 这里不需要更新 data_，因为它指向的是对象内部的灵活数组成员
        // 灵活数组不是指针，不占用内存空间，在访问时通过偏移量动态计算得到
    }

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        ASSERT(layout_, "FixedArray layout must be set via updateLayout before use");
        TypeCode type = layout_->elemType();

        if (!isGCTraced(type))
            return;

        Object **refArr = reinterpret_cast<Object **>(data_);

        for (size_t i = 0; i < size_; ++i) {
            if (Object *&ref = refArr[i]) {
                ref = relocate(ref);
            }
        }
    }

  private:
    explicit FixedArray(size_t size) : layout_(nullptr), size_(size) {}

    const ArrayTypeLayout *layout_; // 使用时通过 updateLayout 设置
    size_t size_;

    // 灵活数组成员 (Flexible Array Member)
    // 必须是最后一个成员
    // alignas 确保正确对齐
    slot_t data_[];
};

// Array: 动态数组，支持小数组内联优化
class Array : public Object {
  public:
    // 禁止直接构造
    Array(const Array &)            = delete;
    Array &operator=(const Array &) = delete;

    static Array *create(IAllocator &allocator, size_t initSize = 0) {
        void *memory = allocator.alloc(sizeof(Array), alignof(Array));
        if (!memory)
            throw std::bad_alloc();

        return new (memory) Array(allocator, initSize);
    }

    size_t size() const { return size_; }
    void resize(size_t newSize) {
        if (newSize > capacity_) {
            reserve(newSize);
        }
        size_ = newSize;
    }
    size_t capacity() const { return capacity_; }
    slot_t *data() { return static_cast<slot_t *>(dataPtr_); }
    const slot_t *data() const { return static_cast<const slot_t *>(dataPtr_); }
    const ArrayTypeLayout &layout() const {
        ASSERT(layout_, "Array layout must be set via updateLayout before use");
        return *layout_;
    }
    void updateLayout(const ArrayTypeLayout *layout) {
        layout_ = layout;
        if (fixedArray_)
            fixedArray_->updateLayout(layout);
    }
    TypeCode elemType() const {
        ASSERT(layout_, "Array layout must be set via updateLayout before use");
        return layout_->elemType();
    }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return fromSlot<T>(dataPtr_[index]);
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        if constexpr (std::is_same_v<T, Object *>) {
            // writeBarrier(arr[index], value);
        }
        dataPtr_[index] = toSlot(value);
    }

    void reserve(size_t newCapacity) {
        ASSERT(layout_, "Array layout must be set via updateLayout before reserve");
        if (newCapacity <= capacity_)
            return;
        reallocate(newCapacity);
    }

    template <typename T> void append(const T value) {
        ASSERT(layout_, "Array layout must be set via updateLayout before append");
        if (size_ >= capacity_) {
            reserve(capacity_ * 3 / 2);
        }

        if constexpr (std::is_same_v<T, Object *>) {
            // writeBarrier(arr[size_], value);
        }
        dataPtr_[size_++] = toSlot(value);
    }

    void pop() {
        ASSERT(size_ > 0, "Cannot pop from empty array");
        size_--;
        // 不需要立即缩容，保持capacity不变
    }

    void clear() {
        size_ = 0;
        // 保留capacity，不释放fixedArray_
    }

    void shrinkToFit() {
        ASSERT(layout_, "Array layout must be set via updateLayout before shrinkToFit");
        if (size_ == capacity_)
            return;
        reallocate(size_ > 0 ? size_ : SMALL_ARRAY_SIZE);
    }

    virtual bool equals(const Object *other, bool deep = false) const override {
        ASSERT(layout_, "Array layout must be set via updateLayout before use");
        if (!isOfSameCls(this, other))
            return false;

        const Array *otherArray = reinterpret_cast<const Array *>(other);
        if (size_ != otherArray->size_ || layout_->elemType() != otherArray->layout_->elemType())
            return false;

        const Object *const *arrA = reinterpret_cast<const Object *const *>(dataPtr_);
        const Object *const *arrB = reinterpret_cast<const Object *const *>(otherArray->dataPtr_);

        if (isGCTraced(layout_->elemType())) {
            if (deep) {
                // 深比较引用对象
                for (size_t i = 0; i < size_; ++i) {
                    const Object *refA = arrA[i];
                    const Object *refB = arrB[i];
                    if (refA == refB)
                        continue;
                    if (!refA->equals(refB, true))
                        return false;
                }
                return true;
            } else {
                // 浅比较引用地址
                return std::memcmp(arrA, arrB, size_ * sizeof(Object *)) == 0;
            }
        } else {
            // 基本类型比较
            return std::memcmp(arrA, arrB, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
        ASSERT(layout_, "Array layout must be set via updateLayout before use");
        Array *newArray = Array::create(allocator, 0);
        newArray->updateLayout(layout_);
        newArray->size_     = size_;
        newArray->capacity_ = capacity_;

        if (fixedArray_) {
            // 外部存储：直接克隆 FixedArray，它会处理内部的元素复制
            newArray->fixedArray_ =
                reinterpret_cast<FixedArray *>(fixedArray_->clone(allocator, deep));
            newArray->dataPtr_ = newArray->fixedArray_->data();
        } else {
            // 内联存储：需要自己复制数据
            newArray->fixedArray_ = nullptr;
            newArray->dataPtr_    = newArray->inlineData_;

            const Object *const *srcArr = reinterpret_cast<const Object *const *>(dataPtr_);
            Object **dstArr             = reinterpret_cast<Object **>(newArray->dataPtr_);

            if (isGCTraced(layout_->elemType())) {
                for (size_t i = 0; i < size_; ++i) {
                    const Object *oriRef = srcArr[i];
                    if (oriRef) {
                        dstArr[i] =
                            deep ? oriRef->clone(allocator, true) : const_cast<Object *>(oriRef);
                    } else {
                        dstArr[i] = NullRef;
                    }
                }
            } else {
                std::memcpy(dstArr, srcArr, size_ * sizeof(slot_t));
            }
        }

        return reinterpret_cast<Object *>(newArray);
    }

    virtual void print(std::ostream &os) const override {
        ASSERT(layout_, "Array layout must be set via updateLayout before use");
        os << "[";

        TypeCode elemType     = layout_->elemType();
        const slot_t *dataPtr = dataPtr_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            printSlot(os, dataPtr[i], elemType);
        }

        os << "]";
    }

    virtual void onMoved() override {
        if (fixedArray_) {
            dataPtr_ = fixedArray_->data();
        } else {
            dataPtr_ = inlineData_;
        }
    }

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        // 更新对GCFixedArray的引用
        if (fixedArray_) {
            Object *newPtr = relocate(fixedArray_);
            fixedArray_    = static_cast<FixedArray *>(newPtr);
            dataPtr_       = fixedArray_->data();
        }
    }

  private:
    static constexpr size_t SMALL_ARRAY_SIZE = 10;

    Array(IAllocator &allocator, size_t initSize)
        : allocator_(&allocator), layout_(nullptr), fixedArray_(nullptr), size_(initSize) {
        if (initSize > SMALL_ARRAY_SIZE) {
            capacity_   = initSize;
            fixedArray_ = FixedArray::create(capacity_, allocator);
            dataPtr_    = fixedArray_->data();
        } else {
            capacity_ = SMALL_ARRAY_SIZE;
            dataPtr_  = inlineData_;
            std::fill(inlineData_, inlineData_ + SMALL_ARRAY_SIZE, NullSlot);
        }
    }

    void reallocate(size_t newCapacity) {
        ASSERT(layout_, "Array layout must be set before reallocate");
        if (UNLIKELY(newCapacity <= SMALL_ARRAY_SIZE)) {
            if (fixedArray_ != nullptr && size_ > 0) {
                std::memcpy(inlineData_, dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = nullptr;
            dataPtr_    = inlineData_;
            capacity_   = SMALL_ARRAY_SIZE;
        } else {
            FixedArray *newArray = FixedArray::create(newCapacity, *allocator_);
            newArray->updateLayout(layout_);
            if (size_ > 0) {
                std::memcpy(newArray->data(), dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = newArray;
            dataPtr_    = newArray->data();
            capacity_   = newCapacity;
        }
    }

    IAllocator *allocator_;
    const ArrayTypeLayout *layout_; // 使用时通过 updateLayout 设置
    FixedArray *fixedArray_;        // 底层固定数组，nullptr表示使用内联存储（8字节）

    uint32_t size_;     // 逻辑元素个数（4字节）
    uint32_t capacity_; // 缓存的容量信息（4字节）

    // 关键字段：数据指针，始终指向当前使用的数据区
    // - 当使用内联存储时：dataPtr_ == inlineData_
    // - 当使用外部数组时：dataPtr_ == fixedArray_->data()
    // 这使得随机访问（operator[]）可以零开销地直接解引用
    slot_t *dataPtr_; // 当前数据区指针（8字节）

    // 小数组优化：内联存储
    slot_t inlineData_[SMALL_ARRAY_SIZE]; // 内联数据区
};
