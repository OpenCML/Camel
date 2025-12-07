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

// GCFixedArray: 固定大小的数组，对象体和数据区在一块连续内存中
class GCFixedArray : public GCObject {
  public:
    // 禁止直接构造
    GCFixedArray(const GCFixedArray &)            = delete;
    GCFixedArray &operator=(const GCFixedArray &) = delete;

    // 静态创建方法
    static GCFixedArray *
    create(TypeCode type, size_t size, IAllocator &allocator = mm::autoSpace()) {
        // 计算总内存大小：对象头 + 数组数据
        // offsetof 会自动考虑 data_ 的对齐要求
        size_t headerSize = offsetof(GCFixedArray, data_);
        size_t dataSize   = size * sizeof(slot_t);
        size_t totalSize  = headerSize + dataSize;

        // 分配连续内存
        // alignof(GCFixedArray) 已经包含了 slot_t 的对齐要求（因为 data_ 的 alignas）
        void *memory = allocator.alloc(totalSize, alignof(GCFixedArray));
        if (!memory)
            throw std::bad_alloc();

        // 在分配的内存上构造对象
        GCFixedArray *array = new (memory) GCFixedArray(type, size);

        if (isGCTraced(type)) {
            // 数据区初始化为空，以免空数据区被当作对象引用
            GCRef *dataStart = reinterpret_cast<GCRef *>(array->data_);
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
        if constexpr (std::is_same_v<T, GCRef>) {
            // writeBarrier(arr[index], value);
        }
        arr[index] = value;
    }

    void onMoved() override {
        // 这里不需要更新 data_，因为它指向的是对象内部的灵活数组成员
        // 灵活数组不是指针，不占用内存空间，在访问时通过偏移量动态计算得到
    }

    void updateRefs(const std::function<GCRef(GCRef)> &relocate) override {
        if (!isGCTraced(type_))
            return;

        GCRef *refArr = reinterpret_cast<GCRef *>(data_);

        for (size_t i = 0; i < size_; ++i) {
            if (GCRef &ref = refArr[i]) {
                ref = relocate(ref);
            }
        }
    }

  private:
    GCFixedArray(TypeCode type, size_t size) : size_(size), type_(type) {}

    uint32_t size_;
    TypeCode type_;

    // 灵活数组成员 (Flexible Array Member)
    // 必须是最后一个成员
    // alignas 确保正确对齐
    alignas(slot_t) std::byte data_[];
};

// GCArray: 动态数组，支持小数组内联优化
class GCArray : public GCObject {
  public:
    // 禁止直接构造
    GCArray(const GCArray &)            = delete;
    GCArray &operator=(const GCArray &) = delete;

    static GCArray *create(TypeCode elemType, IAllocator &allocator = mm::autoSpace()) {
        void *memory = allocator.alloc(sizeof(GCArray), alignof(GCArray));
        if (!memory)
            throw std::bad_alloc();

        return new (memory) GCArray(elemType, allocator);
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
        if constexpr (std::is_same_v<T, GCRef>) {
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
        if constexpr (std::is_same_v<T, GCRef>) {
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

    void onMoved() override {
        if (fixedArray_) {
            dataPtr_ = fixedArray_->data();
        } else {
            dataPtr_ = inlineData_;
        }
    }

    void updateRefs(const std::function<GCRef(GCRef)> &relocate) override {
        // 更新对GCFixedArray的引用
        if (fixedArray_) {
            GCRef newPtr = relocate(fixedArray_);
            fixedArray_  = static_cast<GCFixedArray *>(newPtr);
            dataPtr_     = fixedArray_->data();
        }
    }

  private:
    // 小数组优化的大小，控制整体在一个cacheline内（64字节）
    // sizeof(GCObject) + allocator_(8) + size_(4) + capacity_(4) + type_(4) +
    // fixedArray_(8) + dataPtr_(8) + inlineData_(32) ≈ 68 + GCObject
    // 假设GCObject约8-16字节，总大小约76-84字节，略超一个cacheline但在可接受范围
    static constexpr size_t SMALL_ARRAY_SIZE = 4;

    GCArray(TypeCode type, IAllocator &allocator)
        : allocator_(&allocator), size_(0), capacity_(SMALL_ARRAY_SIZE), type_(type),
          fixedArray_(nullptr), dataPtr_(inlineData_) { // 初始化时指向内联数组
        // 初始化内联数组
        if (isGCTraced(type_)) {
            GCRef *refArr = reinterpret_cast<GCRef *>(inlineData_);
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
            GCFixedArray *newArray = GCFixedArray::create(type_, newCapacity, *allocator_);
            if (size_ > 0) {
                std::memcpy(newArray->data(), dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = newArray;
            dataPtr_    = newArray->data();
            capacity_   = newCapacity;
        }
    }

    IAllocator *allocator_;    // 分配器引用（8字节）
    uint32_t size_;            // 逻辑元素个数（4字节）
    uint32_t capacity_;        // 缓存的容量信息（4字节）
    TypeCode type_;            // 元素类型（4字节）
    GCFixedArray *fixedArray_; // 底层固定数组，nullptr表示使用内联存储（8字节）

    // 关键字段：数据指针，始终指向当前使用的数据区
    // - 当使用内联存储时：dataPtr_ == inlineData_
    // - 当使用外部数组时：dataPtr_ == fixedArray_->data()
    // 这使得随机访问（operator[]）可以零开销地直接解引用
    void *dataPtr_; // 当前数据区指针（8字节）

    // 小数组优化：内联存储
    alignas(slot_t) std::byte inlineData_[SMALL_ARRAY_SIZE * sizeof(slot_t)]; // 32字节
};
