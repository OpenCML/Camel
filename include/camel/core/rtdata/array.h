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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "camel/core/type/composite/array.h"

#include <algorithm>

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

// FixedArray: fixed-size array with object body and data area in one contiguous block.
class FixedArray : public rtdata::Object {
  public:
    // Direct construction is forbidden.
    FixedArray(const FixedArray &)            = delete;
    FixedArray &operator=(const FixedArray &) = delete;

    // Static creation method.
    static FixedArray *create(size_t size, camel::core::mm::IAllocator &allocator) {
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

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return fromSlot<T>(data_[index]);
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        if constexpr (std::is_same_v<T, rtdata::Object *>) {
            // writeBarrier(arr[index], value);
        }
        data_[index] = toSlot(value);
    }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        const FixedArray *otherArray = reinterpret_cast<const FixedArray *>(other);
        if (size_ != otherArray->size_)
            return false;

        const rtdata::Object *const *arrA = reinterpret_cast<const rtdata::Object *const *>(data_);
        const rtdata::Object *const *arrB =
            reinterpret_cast<const rtdata::Object *const *>(otherArray->data_);

        if (type::isGCTraced(elemTypeCode)) {
            if (deep) {
                // Deep compare reference objects.
                const type::Type *elemType = arrayType->elemType();
                for (size_t i = 0; i < size_; ++i) {
                    const rtdata::Object *refA = arrA[i];
                    const rtdata::Object *refB = arrB[i];
                    if (refA == refB)
                        continue;
                    if (!refA->equals(refB, elemType, true))
                        return false;
                }
                return true;
            } else {
                // Shallow compare reference pointer addresses.
                return std::memcmp(arrA, arrB, size_ * sizeof(rtdata::Object *)) == 0;
            }
        } else {
            return std::memcmp(arrA, arrB, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        FixedArray *newArray = FixedArray::create(size_, allocator);
        const rtdata::Object *const *srcArr =
            reinterpret_cast<const rtdata::Object *const *>(data_);
        rtdata::Object **dstArr = reinterpret_cast<rtdata::Object **>(newArray->data_);

        if (type::isGCTraced(elemTypeCode)) {
            for (size_t i = 0; i < size_; ++i) {
                const rtdata::Object *oriRef = srcArr[i];
                if (oriRef) {
                    if (deep) {
                        // Deep copy: recursively clone referenced objects.
                        rtdata::Object *clonedRef =
                            oriRef->clone(allocator, arrayType->elemType(), true);
                        dstArr[i] = clonedRef;
                    } else {
                        // Shallow copy: copy the reference directly.
                        dstArr[i] = const_cast<rtdata::Object *>(oriRef);
                    }
                } else {
                    dstArr[i] = rtdata::NullRef;
                }
            }
        } else {
            // Non-reference types: copy data directly.
            std::memcpy(dstArr, srcArr, size_ * sizeof(slot_t));
        }

        return reinterpret_cast<rtdata::Object *>(newArray);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::Type *elemType             = arrayType->elemType();

        os << "[";

        const slot_t *dataPtr = data_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            rtdata::printSlot(os, dataPtr[i], elemType);
        }

        os << "]";
    }

    virtual void onMoved() override {
        // There is no need to update data_ here, because it points to the object's internal
        // flexible array member. The flexible array is not a pointer and occupies no extra memory;
        // its address is computed by offset at access time.
    }

    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override {
        if (!type || type->code() != type::TypeCode::Array)
            return;
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        if (!type::isGCTraced(elemTypeCode))
            return;

        rtdata::Object **refArr = reinterpret_cast<rtdata::Object **>(data_);

        for (size_t i = 0; i < size_; ++i) {
            if (rtdata::Object *&ref = refArr[i]) {
                ref = relocate(ref);
            }
        }
    }

  private:
    explicit FixedArray(size_t size) : size_(size) {}

    size_t size_;

    // Flexible array member (Flexible Array Member)
    // Must be the last member.
    // alignas ensures correct alignment.
    slot_t data_[];
};

// Array: dynamic array with small-array inline optimization.
class Array : public rtdata::Object {
  public:
    // Direct construction is forbidden.
    Array(const Array &)            = delete;
    Array &operator=(const Array &) = delete;

    static Array *create(camel::core::mm::IAllocator &allocator, size_t initSize = 0) {
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

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return rtdata::fromSlot<T>(dataPtr_[index]);
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        if constexpr (std::is_same_v<T, rtdata::Object *>) {
            // writeBarrier(arr[index], value);
        }
        dataPtr_[index] = rtdata::toSlot(value);
    }

    void reserve(size_t newCapacity) {
        if (newCapacity <= capacity_)
            return;
        reallocate(newCapacity);
    }

    template <typename T> void append(const T value) {
        if (size_ >= capacity_) {
            reserve(capacity_ * 3 / 2);
        }

        if constexpr (std::is_same_v<T, rtdata::Object *>) {
            // writeBarrier(arr[size_], value);
        }
        dataPtr_[size_++] = rtdata::toSlot(value);
    }

    void pop() {
        ASSERT(size_ > 0, "Cannot pop from empty array");
        size_--;
        // No immediate shrink is needed; keep capacity unchanged.
    }

    void clear() {
        size_ = 0;
        // Preserve capacity and do not release fixedArray_.
    }

    void shrinkToFit() {
        if (size_ == capacity_)
            return;
        reallocate(size_ > 0 ? size_ : SMALL_ARRAY_SIZE);
    }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        if (!isOfSameCls(this, other))
            return false;

        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        const Array *otherArray = reinterpret_cast<const Array *>(other);
        if (size_ != otherArray->size_)
            return false;

        const rtdata::Object *const *arrA =
            reinterpret_cast<const rtdata::Object *const *>(dataPtr_);
        const rtdata::Object *const *arrB =
            reinterpret_cast<const rtdata::Object *const *>(otherArray->dataPtr_);

        if (type::isGCTraced(elemTypeCode)) {
            if (deep) {
                // Deep compare referenced objects.
                for (size_t i = 0; i < size_; ++i) {
                    const rtdata::Object *refA = arrA[i];
                    const rtdata::Object *refB = arrB[i];
                    if (refA == refB)
                        continue;
                    if (!refA->equals(refB, arrayType->elemType(), true))
                        return false;
                }
                return true;
            } else {
                // Shallow compare reference addresses.
                return std::memcmp(arrA, arrB, size_ * sizeof(rtdata::Object *)) == 0;
            }
        } else {
            // Compare primitive values.
            return std::memcmp(arrA, arrB, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        Array *newArray     = Array::create(allocator, 0);
        newArray->size_     = size_;
        newArray->capacity_ = capacity_;

        if (fixedArray_) {
            // External storage: clone FixedArray directly.
            newArray->fixedArray_ =
                reinterpret_cast<FixedArray *>(fixedArray_->clone(allocator, type, deep));
            newArray->dataPtr_ = newArray->fixedArray_->data();
        } else {
            // Inline storage: copy the data ourselves.
            newArray->fixedArray_ = nullptr;
            newArray->dataPtr_    = newArray->inlineData_;

            const rtdata::Object *const *srcArr =
                reinterpret_cast<const rtdata::Object *const *>(dataPtr_);
            rtdata::Object **dstArr = reinterpret_cast<rtdata::Object **>(newArray->dataPtr_);

            if (type::isGCTraced(elemTypeCode)) {
                for (size_t i = 0; i < size_; ++i) {
                    const rtdata::Object *oriRef = srcArr[i];
                    if (oriRef) {
                        dstArr[i] = deep ? oriRef->clone(allocator, arrayType->elemType(), true)
                                         : const_cast<rtdata::Object *>(oriRef);
                    } else {
                        dstArr[i] = rtdata::NullRef;
                    }
                }
            } else {
                std::memcpy(dstArr, srcArr, size_ * sizeof(slot_t));
            }
        }

        return reinterpret_cast<rtdata::Object *>(newArray);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::Type *elemType             = arrayType->elemType();

        os << "[";

        const slot_t *dataPtr = dataPtr_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            rtdata::printSlot(os, dataPtr[i], elemType);
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

    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override {
        if (!type || type->code() != type::TypeCode::Array)
            return;
        // Update the reference to FixedArray.
        if (fixedArray_) {
            rtdata::Object *newPtr = relocate(fixedArray_);
            fixedArray_            = static_cast<FixedArray *>(newPtr);
            dataPtr_               = fixedArray_->data();
        }
    }

  private:
    static constexpr size_t SMALL_ARRAY_SIZE = 10;

    Array(camel::core::mm::IAllocator &allocator, size_t initSize)
        : allocator_(&allocator), fixedArray_(nullptr), size_(initSize) {
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
        if (UNLIKELY(newCapacity <= SMALL_ARRAY_SIZE)) {
            if (fixedArray_ != nullptr && size_ > 0) {
                std::memcpy(inlineData_, dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = nullptr;
            dataPtr_    = inlineData_;
            capacity_   = SMALL_ARRAY_SIZE;
        } else {
            FixedArray *newArray = FixedArray::create(newCapacity, *allocator_);
            if (size_ > 0) {
                std::memcpy(newArray->data(), dataPtr_, size_ * sizeof(slot_t));
            }
            fixedArray_ = newArray;
            dataPtr_    = newArray->data();
            capacity_   = newCapacity;
        }
    }

    camel::core::mm::IAllocator *allocator_;
    FixedArray *fixedArray_; // Backing fixed array; nullptr means inline storage (8 bytes).

    uint32_t size_;     // Logical element count (4 bytes).
    uint32_t capacity_; // Cached capacity (4 bytes).

    // Key field: data pointer, always points to the active data region.
    // - When inline storage is active: dataPtr_ == inlineData_
    // - When using external storage: dataPtr_ == fixedArray_->data()
    // This makes random access (operator[]) zero-overhead direct dereference.
    slot_t *dataPtr_; // Pointer to the current data region (8 bytes).

    // Small-array optimization: inline storage.
    slot_t inlineData_[SMALL_ARRAY_SIZE]; // Inline data region.
};
