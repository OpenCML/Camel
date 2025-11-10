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
 * Updated: Nov. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class GCArray : public GCObject {
  public:
    GCArray(TypeCode typeCode)
        : type_(typeCode), layout_(getLayoutOfType(typeCode)), data_(nullptr), size_(0),
          capacity_(0) {}

    size_t size() const { return size_; }
    TypeCode typeCode() const { return type_; }

    void *data() { return data_; }
    const void *data() const { return data_; }

    template <typename T> T &operator[](size_t index) { return at<T>(index); }

    template <typename T> const T &operator[](size_t index) const { return at<T>(index); }

    template <typename T> T &at(size_t index) {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == layout_.size, "Type size mismatch");
        ASSERT(alignof(T) <= layout_.align, "Type alignment mismatch");
        return *reinterpret_cast<T *>(static_cast<uint8_t *>(data_) + index * layout_.size);
    }

    template <typename T> const T &at(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        ASSERT(sizeof(T) == layout_.size, "Type size mismatch");
        ASSERT(alignof(T) <= layout_.align, "Type alignment mismatch");
        return *reinterpret_cast<const T *>(
            static_cast<const uint8_t *>(data_) + index * layout_.size);
    }

    void reserve(size_t size) {
        if (size <= capacity_)
            return;

        size_t totalSize = size * layout_.size;
        totalSize        = alignUp(totalSize, layout_.align);

        void *newData = GlobalGC::instance().allocate(totalSize, layout_.align);
        if (!newData)
            throw std::bad_alloc();

        if (data_) {
            std::memcpy(newData, data_, size_ * layout_.size);
        }

        data_     = newData;
        capacity_ = size;
    }

    template <typename T> void append(const T &value) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        ASSERT(sizeof(T) == layout_.size, "Type size mismatch");
        ASSERT(alignof(T) <= layout_.align, "Type alignment mismatch");

        if (size_ >= capacity_) {
            size_t newCap = capacity_ == 0 ? 1 : capacity_ * 2;
            reserve(newCap);
        }

        void *dest = static_cast<uint8_t *>(data_) + size_ * layout_.size;
        std::memcpy(dest, &value, layout_.size);
        size_++;
    }

    // GCObject 接口实现
    void trace(const std::function<void(GCObject *)> &visit) const override {
        if (!isGCTraced(type_))
            return; // 非 GCObject 类型，无需追踪

        for (size_t i = 0; i < size_; ++i) {
            auto objPtr = *reinterpret_cast<GCObject *const *>(
                static_cast<const uint8_t *>(data_) + i * layout_.size);
            if (objPtr) {
                visit(objPtr);
            }
        }
    }

    ObjectHeader *header() const override {
        return reinterpret_cast<ObjectHeader *>(
            reinterpret_cast<uint8_t *>(data_) - sizeof(ObjectHeader));
    }

  private:
    TypeCode type_;
    DataLayout layout_;

    void *data_;
    size_t size_;
    size_t capacity_;
};

static_assert(alignof(GCArray) == 8, "GCArray alignment mismatch");
static_assert(sizeof(GCArray) == 32 + 8, "GCArray size mismatch");
