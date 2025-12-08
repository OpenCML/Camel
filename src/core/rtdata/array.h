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
#include "core/mm/mm.h"
#include "core/type/type.h"

// FixedArray: 固定大小的数组，对象体和数据区在一块连续内存中
class FixedArray : public Object {
  public:
    // 禁止直接构造
    FixedArray(const FixedArray &)            = delete;
    FixedArray &operator=(const FixedArray &) = delete;

    // 静态创建方法
    static FixedArray *create(TypeCode type, size_t size, IAllocator &allocator = mm::autoSpace()) {
        // 计算总内存大小：对象头 + 数组数据
        // offsetof 会自动考虑 data_ 的对齐要求
        size_t headerSize = offsetof(FixedArray, data_);
        size_t dataSize   = size * sizeof(slot_t);
        size_t totalSize  = headerSize + dataSize;

        // 分配连续内存
        // alignof(FixedArray) 已经包含了 slot_t 的对齐要求（因为 data_ 的 alignas）
        void *memory = allocator.alloc(totalSize, alignof(FixedArray));
        if (!memory)
            throw std::bad_alloc();

        // 在分配的内存上构造对象
        FixedArray *array = new (memory) FixedArray(type, size);

        if (isGCTraced(type)) {
            // 数据区初始化为空，以免空数据区被当作对象引用
            Object **dataStart = reinterpret_cast<Object **>(array->data_);
            std::fill(dataStart, dataStart + size, NullRef);
        }

        return array;
    }

    size_t size() const { return size_; }
    void *data() { return data_; }
    const void *data() const { return data_; }

    TypeCode elemType() const { return type_; }

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

    virtual bool equals(const Object *other, bool deep = false) const override {
        const FixedArray *otherArray = reinterpret_cast<const FixedArray *>(other);
        if (this == otherArray)
            return true;
        if (!isOfSameCls(this, otherArray))
            return false;
        if (size_ != otherArray->size_ || type_ != otherArray->type_)
            return false;

        if (isGCTraced(type_)) {
            const Object **dataA = reinterpret_cast<const Object **>(data_);
            const Object **dataB = reinterpret_cast<const Object **>(otherArray->data_);

            if (deep) {
                // 深比较引用对象
                for (size_t i = 0; i < size_; ++i) {
                    const Object *refA = dataA[i];
                    const Object *refB = dataB[i];
                    if (!refA->equals(refB, true))
                        return false;
                }
                return true;
            } else {
                // 浅比较引用指针地址
                return std::memcmp(dataA, dataB, size_ * sizeof(Object *)) == 0;
            }
        } else {
            return std::memcmp(data_, otherArray->data_, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual Object *
    clone(IAllocator &allocator = mm::autoSpace(), bool deep = false) const override {
        FixedArray *newArray = FixedArray::create(type_, size_, allocator);

        if (isGCTraced(type_)) {
            const Object **srcData = reinterpret_cast<const Object **>(data_);
            Object **dstData       = reinterpret_cast<Object **>(newArray->data_);

            for (size_t i = 0; i < size_; ++i) {
                Object *originalRef = srcData[i];
                if (originalRef) {
                    if (deep) {
                        // 深拷贝：递归克隆引用对象
                        Object *obj       = reinterpret_cast<Object *>(originalRef);
                        Object *clonedRef = obj->clone(allocator, true);
                        dstData[i]        = clonedRef;
                    } else {
                        // 浅拷贝：直接复制引用
                        dstData[i] = originalRef;
                    }
                } else {
                    dstData[i] = NullRef;
                }
            }
        } else {
            // 非引用类型，直接复制数据
            std::memcpy(newArray->data_, data_, size_ * sizeof(slot_t));
        }

        return reinterpret_cast<Object *>(newArray);
    }

    virtual void onMoved() override {
        // 这里不需要更新 data_，因为它指向的是对象内部的灵活数组成员
        // 灵活数组不是指针，不占用内存空间，在访问时通过偏移量动态计算得到
    }

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        if (!isGCTraced(type_))
            return;

        Object **refArr = reinterpret_cast<Object **>(data_);

        for (size_t i = 0; i < size_; ++i) {
            if (Object *&ref = refArr[i]) {
                ref = relocate(ref);
            }
        }
    }

  private:
    FixedArray(TypeCode type, size_t size) : size_(size), type_(type) {}

    uint32_t size_;
    TypeCode type_;

    // 灵活数组成员 (Flexible Array Member)
    // 必须是最后一个成员
    // alignas 确保正确对齐
    alignas(slot_t) std::byte data_[];
};

// Array: 动态数组，支持小数组内联优化
class Array : public Object {
  public:
    // 禁止直接构造
    Array(const Array &)            = delete;
    Array &operator=(const Array &) = delete;

    static Array *create(TypeCode elemType, IAllocator &allocator = mm::autoSpace()) {
        void *memory = allocator.alloc(sizeof(Array), alignof(Array));
        if (!memory)
            throw std::bad_alloc();

        return new (memory) Array(elemType, allocator);
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");
        return reinterpret_cast<const T *>(dataPtr_)[index];
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");

        T *arr = reinterpret_cast<T *>(dataPtr_);
        if constexpr (std::is_same_v<T, Object *>) {
            // writeBarrier(arr[index], value);
        }
        arr[index] = value;
    }

    void reserve(size_t newCapacity) {
        if (newCapacity <= capacity_)
            return;
        reallocate(newCapacity);
    }

    template <typename T> void append(const T value) {
        ASSERT(sizeof(T) == sizeof(slot_t), "Type size mismatch");
        ASSERT(alignof(T) <= alignof(slot_t), "Type alignment mismatch");

        // 检查是否需要扩容
        if (size_ >= capacity_) {
            // 类似std::vector，增长1.5倍或2倍
            // capacity_ 初始化为 SMALL_ARRAY_SIZE，不会为0
            reserve(capacity_ * 3 / 2);
        }

        T *arr = reinterpret_cast<T *>(dataPtr_);
        if constexpr (std::is_same_v<T, Object *>) {
            // writeBarrier(arr[size_], value);
        }
        arr[size_++] = value;
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
        const Array *otherArray = reinterpret_cast<const Array *>(other);

        if (this == otherArray)
            return true;
        if (!isOfSameCls(this, otherArray))
            return false;

        if (type_ != otherArray->type_ || size_ != otherArray->size_)
            return false;

        const void *lhsData = dataPtr_;
        const void *rhsData = otherArray->dataPtr_;

        if (isGCTraced(type_)) {
            const Object **lhs = reinterpret_cast<const Object **>(lhsData);
            const Object **rhs = reinterpret_cast<const Object **>(rhsData);

            if (deep) {
                for (size_t i = 0; i < size_; ++i) {
                    const Object *la = lhs[i];
                    const Object *rb = rhs[i];
                    if (!la->equals(rb, true))
                        return false;
                }
                return true;
            } else {
                // 浅比较（只比较引用地址）
                return std::memcmp(lhs, rhs, size_ * sizeof(Object *)) == 0;
            }
        }

        return std::memcmp(lhsData, rhsData, size_ * sizeof(slot_t)) == 0;
    }

    virtual Object *
    clone(IAllocator &allocator = mm::autoSpace(), bool deep = false) const override {
        Array *newArray = Array::create(type_, allocator);

        newArray->size_     = size_;
        newArray->capacity_ = capacity_;

        if (fixedArray_) {
            Object *clonedFixed   = fixedArray_->clone(allocator, deep);
            newArray->fixedArray_ = reinterpret_cast<FixedArray *>(clonedFixed);
            newArray->dataPtr_    = newArray->fixedArray_->data();
        } else {
            // 内联存储
            newArray->fixedArray_ = nullptr;
            newArray->dataPtr_    = newArray->inlineData_;

            if (isGCTraced(type_)) {
                const Object **src = reinterpret_cast<const Object **>(inlineData_);
                Object **dst       = reinterpret_cast<Object **>(newArray->inlineData_);
                for (size_t i = 0; i < size_; ++i) {
                    if (const Object *ref = src[i]) {
                        dst[i] = deep
                                     ? reinterpret_cast<const Object *>(ref)->clone(allocator, true)
                                     : ref;
                    } else {
                        dst[i] = NullRef;
                    }
                }
            } else {
                std::memcpy(newArray->inlineData_, inlineData_, size_ * sizeof(slot_t));
            }
        }

        return reinterpret_cast<Object *>(newArray);
    }

    void onMoved() override {
        if (fixedArray_) {
            dataPtr_ = fixedArray_->data();
        } else {
            dataPtr_ = inlineData_;
        }
    }

    void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        // 更新对GCFixedArray的引用
        if (fixedArray_) {
            Object *newPtr = relocate(fixedArray_);
            fixedArray_    = static_cast<FixedArray *>(newPtr);
            dataPtr_       = fixedArray_->data();
        }
    }

  private:
    // 小数组优化的大小，控制整体在一个cacheline内（64字节）
    // sizeof(Object) + allocator_(8) + size_(4) + capacity_(4) + type_(4) +
    // fixedArray_(8) + dataPtr_(8) + inlineData_(32) ≈ 68 + Object
    // 假设GCObject约8-16字节，总大小约76-84字节，略超一个cacheline但在可接受范围
    static constexpr size_t SMALL_ARRAY_SIZE = 4;

    Array(TypeCode type, IAllocator &allocator)
        : allocator_(&allocator), size_(0), capacity_(SMALL_ARRAY_SIZE), type_(type),
          fixedArray_(nullptr), dataPtr_(inlineData_) { // 初始化时指向内联数组
        // 初始化内联数组
        if (isGCTraced(type_)) {
            Object **refArr = reinterpret_cast<Object **>(inlineData_);
            std::fill(refArr, refArr + SMALL_ARRAY_SIZE, NullRef);
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
            FixedArray *newArray = FixedArray::create(type_, newCapacity, *allocator_);
            if (size_ > 0) {
                std::memcpy(newArray->data(), dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = newArray;
            dataPtr_    = newArray->data();
            capacity_   = newCapacity;
        }
    }

    IAllocator *allocator_;  // 分配器引用（8字节）
    uint32_t size_;          // 逻辑元素个数（4字节）
    uint32_t capacity_;      // 缓存的容量信息（4字节）
    TypeCode type_;          // 元素类型（4字节）
    FixedArray *fixedArray_; // 底层固定数组，nullptr表示使用内联存储（8字节）

    // 关键字段：数据指针，始终指向当前使用的数据区
    // - 当使用内联存储时：dataPtr_ == inlineData_
    // - 当使用外部数组时：dataPtr_ == fixedArray_->data()
    // 这使得随机访问（operator[]）可以零开销地直接解引用
    void *dataPtr_; // 当前数据区指针（8字节）

    // 小数组优化：内联存储
    alignas(slot_t) std::byte inlineData_[SMALL_ARRAY_SIZE * sizeof(slot_t)]; // 32字节
};
