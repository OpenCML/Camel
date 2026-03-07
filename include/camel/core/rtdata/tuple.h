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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Tuple : public rtdata::Object {
  public:
    Tuple(const Tuple &)            = delete;
    Tuple &operator=(const Tuple &) = delete;

    /// 创建 Tuple 实例
    static Tuple *create(size_t slotCount, camel::core::mm::IAllocator &allocator) {
        size_t headerSize = sizeof(Tuple);
        size_t dataSize   = sizeof(slot_t) * slotCount;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Tuple));
        if (!memory)
            throw std::bad_alloc();

        Tuple *t = new (memory) Tuple(slotCount);
        std::fill(t->data_, t->data_ + slotCount, NullSlot);
        return t;
    }

    size_t size() const { return size_; }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return rtdata::fromSlot<T>(data_[index]);
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        if constexpr (std::is_same_v<T, rtdata::Object *>) {
            // writeBarrier(arr[index], value);
        }
        data_[index] = rtdata::toSlot(value);
    }

    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Tuple, "Type must be TupleType");
        if (!isOfSameCls(this, other))
            return false;

        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        const Tuple *otherTuple          = reinterpret_cast<const Tuple *>(other);
        if (size_ != otherTuple->size_ || size_ != tupleType->size())
            return false;

        auto codes          = tupleType->codes();
        const slot_t *dataA = data_;
        const slot_t *dataB = otherTuple->data_;

        if (deep) {
            // 深比较：引用类型递归比较，普通类型直接值比较
            for (size_t i = 0; i < size_; ++i) {
                if (type::isGCTraced(codes[i])) {
                    const rtdata::Object *refA =
                        reinterpret_cast<const rtdata::Object *const *>(dataA)[i];
                    const rtdata::Object *refB =
                        reinterpret_cast<const rtdata::Object *const *>(dataB)[i];
                    if (refA == refB)
                        continue;
                    if (!refA->equals(refB, tupleType->typeAt(i), true))
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

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Tuple, "Type must be TupleType");
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        auto codes                       = tupleType->codes();

        Tuple *newTuple   = Tuple::create(size_, allocator);
        const slot_t *src = data_;
        slot_t *dst       = newTuple->data_;

        for (size_t i = 0; i < size_; ++i) {
            if (type::isGCTraced(codes[i])) {
                const rtdata::Object *oriRef =
                    reinterpret_cast<const rtdata::Object *const *>(src)[i];
                rtdata::Object *newRef = rtdata::NullRef;

                if (oriRef) {
                    if (deep) {
                        // 深拷贝：递归克隆引用对象
                        newRef = oriRef->clone(allocator, tupleType->typeAt(i), true);
                    } else {
                        // 浅拷贝：仅复制引用指针
                        newRef = const_cast<rtdata::Object *>(oriRef);
                    }
                }
                reinterpret_cast<rtdata::Object **>(dst)[i] = newRef;
            } else {
                // 非引用类型，直接复制 slot 数据
                dst[i] = src[i];
            }
        }

        return reinterpret_cast<rtdata::Object *>(newTuple);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        ASSERT(type && type->code() == type::TypeCode::Tuple, "Type must be TupleType");
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);

        os << "(";

        const slot_t *dataPtr = data_;

        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            rtdata::printSlot(os, dataPtr[i], tupleType->typeAt(i));
        }

        os << ")";
    }

    virtual void onMoved() override {}

    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override {
        if (!type || type->code() != type::TypeCode::Tuple)
            return;
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        auto codes                       = tupleType->codes();
        rtdata::Object **refArr          = reinterpret_cast<rtdata::Object **>(data_);

        for (size_t i = 0; i < size_; ++i) {
            if (type::isGCTraced(codes[i])) {
                if (rtdata::Object *&ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    explicit Tuple(size_t slotCount) : size_(static_cast<uint32_t>(slotCount)) {}

    uint32_t size_;
    slot_t data_[];
};
