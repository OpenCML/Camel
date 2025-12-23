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
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

// FixedArray: 固定大小的数组，对象体和数据区在一块连续内存中
class FixedArray : public Object {
  public:
    // 禁止直接构造
    FixedArray(const FixedArray &)            = delete;
    FixedArray &operator=(const FixedArray &) = delete;

    // 静态创建方法
    static FixedArray *create(const ArrayTypeLayout &layout, size_t size, IAllocator &allocator) {
        // 计算总内存大小：对象头 + 数组数据
        size_t headerSize = sizeof(FixedArray);
        size_t dataSize   = size * sizeof(slot_t);
        size_t totalSize  = headerSize + dataSize;

        // 分配连续内存
        // alignof(FixedArray) 已经包含了 slot_t 的对齐要求（因为 data_ 的 alignas）
        void *memory = allocator.alloc(totalSize, alignof(FixedArray));
        if (!memory)
            throw std::bad_alloc();

        // 在分配的内存上构造对象
        FixedArray *array = new (memory) FixedArray(layout, size);

        if (isGCTraced(layout.elemType())) {
            // 数据区初始化为空，以免空数据区被当作对象引用
            Object **dataStart = reinterpret_cast<Object **>(array->data_);
            std::fill(dataStart, dataStart + size, NullRef);
        }

        return array;
    }

    size_t size() const { return size_; }
    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }
    const ArrayTypeLayout *layout() const { return layout_; }
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
        FixedArray *newArray        = FixedArray::create(*layout_, size_, allocator);
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
    FixedArray(const ArrayTypeLayout &layout, size_t size) : layout_(&layout), size_(size) {}

    const ArrayTypeLayout *layout_;
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

    static Array *
    create(const ArrayTypeLayout &layout, IAllocator &allocator, size_t initSize = 0) {
        void *memory = allocator.alloc(sizeof(Array), alignof(Array));
        if (!memory)
            throw std::bad_alloc();

        return new (memory) Array(layout, allocator, initSize);
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
    const ArrayTypeLayout &layout() const { return *layout_; }
    void updateLayout(const ArrayTypeLayout *layout) { layout_ = layout; }
    TypeCode elemType() const { return layout_->elemType(); }

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
        if (newCapacity <= capacity_)
            return;
        reallocate(newCapacity);
    }

    template <typename T> void append(const T value) {
        // 检查是否需要扩容
        if (size_ >= capacity_) {
            // 类似std::vector，增长1.5倍或2倍
            // capacity_ 初始化为 SMALL_ARRAY_SIZE，不会为0
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
        if (size_ == capacity_)
            return;
        reallocate(size_ > 0 ? size_ : SMALL_ARRAY_SIZE);
    }

    virtual bool equals(const Object *other, bool deep = false) const override {
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
        Array *newArray     = Array::create(*layout_, allocator);
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

    Array(const ArrayTypeLayout &layout, IAllocator &allocator, size_t initSize)
        : allocator_(&allocator), layout_(&layout), fixedArray_(nullptr), size_(initSize) {
        if (initSize > SMALL_ARRAY_SIZE) {
            // 使用外部数组
            capacity_   = initSize;
            fixedArray_ = FixedArray::create(layout, capacity_, allocator);
            dataPtr_    = fixedArray_->data();
        } else {
            // 使用内联存储
            capacity_ = SMALL_ARRAY_SIZE;
            dataPtr_  = inlineData_;
            // 初始化内联数组
            if (isGCTraced(layout_->elemType())) {
                Object **refArr = reinterpret_cast<Object **>(inlineData_);
                std::fill(refArr, refArr + SMALL_ARRAY_SIZE, NullRef);
            }
        }
    }

    void reallocate(size_t newCapacity) {
        if (UNLIKELY(newCapacity <= SMALL_ARRAY_SIZE)) {
            // 使用内联存储
            if (fixedArray_ != nullptr && size_ > 0) {
                std::memcpy(inlineData_, dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = nullptr;
            dataPtr_    = inlineData_;
            capacity_   = SMALL_ARRAY_SIZE;
        } else {
            // 使用外部数组
            FixedArray *newArray = FixedArray::create(*layout_, newCapacity, *allocator_);
            if (size_ > 0) {
                std::memcpy(newArray->data(), dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = newArray;
            dataPtr_    = newArray->data();
            capacity_   = newCapacity;
        }
    }

    IAllocator *allocator_;         // 分配器引用（8字节）
    const ArrayTypeLayout *layout_; // 类型布局（8字节）
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
