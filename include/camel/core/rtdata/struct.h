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
#include "camel/core/type/composite/struct.h"

#include <algorithm>

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Struct : public rtdata::Object {
  public:
    Struct(const Struct &)            = delete;
    Struct &operator=(const Struct &) = delete;

    static Struct *create(size_t fieldCount, camel::core::mm::IAllocator &allocator) {
        size_t headerSize = sizeof(Struct);
        size_t dataSize   = sizeof(slot_t) * fieldCount;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Struct));
        if (!memory)
            throw std::bad_alloc();

        Struct *s = new (memory) Struct(fieldCount);

        // Standard slot container: initialize everything to NullSlot (references are null;
        // value types are overwritten by set).
        std::fill(s->data_, s->data_ + fieldCount, NullSlot);

        return s;
    }

    size_t size() const { return size_; }

    bool has(std::string_view name, const type::Type *type) const {
        ASSERT(type && type->code() == type::TypeCode::Struct, "Type must be StructType");
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        return structType->findField(name).has_value();
    }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return rtdata::fromSlot<T>(data_[index]);
    }

    template <typename T> T get(std::string_view name, const type::Type *type) const {
        ASSERT(type && type->code() == type::TypeCode::Struct, "Type must be StructType");
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto optIndex                      = structType->findField(name);
        ASSERT(optIndex.has_value(), std::format("Field name not found: {}", name));
        return get<T>(optIndex.value());
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, std::format("Index out of range: {}", index));
        if constexpr (std::is_same_v<T, rtdata::Object *>) {
            // writeBarrier(arr[index], value);
        }
        data_[index] = rtdata::toSlot(value);
    }

    template <typename T> void set(std::string_view name, T value, const type::Type *type) {
        ASSERT(type && type->code() == type::TypeCode::Struct, "Type must be StructType");
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto optIndex                      = structType->findField(name);
        ASSERT(optIndex.has_value(), std::format("Field name not found: {}", name));
        set<T>(optIndex.value(), value);
    }

    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Struct, "Type must be StructType");
        if (!isOfSameCls(this, other))
            return false;

        const type::StructType *structType = static_cast<const type::StructType *>(type);
        const Struct *otherStruct          = reinterpret_cast<const Struct *>(other);

        // Different field counts mean inequality.
        if (size_ != otherStruct->size_ || size_ != structType->size())
            return false;

        auto codes          = structType->codes();
        const slot_t *dataA = data_;
        const slot_t *dataB = otherStruct->data_;

        if (!deep) {
            // Shallow compare: compare the entire memory block directly (reference fields
            // compare pointers only).
            return std::memcmp(dataA, dataB, size_ * sizeof(slot_t)) == 0;
        }

        // Deep compare: recurse field by field.
        for (size_t i = 0; i < size_; ++i) {
            type::TypeCode typeCode = codes[i];
            if (type::isGCTraced(typeCode)) {
                const rtdata::Object *refA =
                    reinterpret_cast<const rtdata::Object *const *>(dataA)[i];
                const rtdata::Object *refB =
                    reinterpret_cast<const rtdata::Object *const *>(dataB)[i];
                if (refA == refB)
                    continue;
                if (!refA->equals(refB, structType->typeAt(i), true))
                    return false;
            } else {
                if (dataA[i] != dataB[i])
                    return false;
            }
        }

        return true;
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Struct, "Type must be StructType");
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto codes                         = structType->codes();

        Struct *newStruct = Struct::create(size_, allocator);
        const slot_t *src = data_;
        slot_t *dst       = newStruct->data_;

        for (size_t i = 0; i < size_; ++i) {
            if (type::isGCTraced(codes[i])) {
                const rtdata::Object *oriRef =
                    reinterpret_cast<const rtdata::Object *const *>(src)[i];
                rtdata::Object *newRef = rtdata::NullRef;

                if (oriRef) {
                    if (deep) {
                        // Recursively clone the referenced object.
                        newRef = oriRef->clone(allocator, structType->typeAt(i), true);
                    } else {
                        // Shallow copy: keep the original pointer.
                        newRef = const_cast<rtdata::Object *>(oriRef);
                    }
                }

                reinterpret_cast<rtdata::Object **>(dst)[i] = newRef;
            } else {
                // Non-reference types: copy the slot data directly.
                dst[i] = src[i];
            }
        }

        return reinterpret_cast<rtdata::Object *>(newStruct);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        ASSERT(type && type->code() == type::TypeCode::Struct, "Type must be StructType");
        if (size_ == 0) {
            os << "{}";
            return;
        }

        const type::StructType *structType = static_cast<const type::StructType *>(type);

        os << "{ ";

        const slot_t *dataPtr = data_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";

            os << structType->fieldName(i) << ": ";
            rtdata::printSlot(os, dataPtr[i], structType->typeAt(i));
        }

        os << " }";
    }

    virtual void onMoved() override {
        // No adjustment is needed.
    }

    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override {
        if (!type || type->code() != type::TypeCode::Struct)
            return;
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto codes                         = structType->codes();
        rtdata::Object **refArr            = reinterpret_cast<rtdata::Object **>(data_);
        for (size_t i = 0; i < size_; ++i) {
            if (type::isGCTraced(codes[i])) {
                if (rtdata::Object *&ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    explicit Struct(size_t fieldCount) : size_(static_cast<uint32_t>(fieldCount)) {}

    uint32_t size_;
    slot_t data_[]; // Flexible array member.
};
