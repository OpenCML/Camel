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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/mm/alloc/allocator.h"
#include "core/type/type.h"
#include "utils/brpred.h"

#include <functional>

class Object {
  public:
    virtual ~Object()                                                          = default;
    virtual bool equals(const Object *other, bool deep = false) const          = 0;
    virtual Object *clone(IAllocator &allocator, bool deep = false) const      = 0;
    virtual void print(std::ostream &os) const                                 = 0;
    virtual void onMoved()                                                     = 0;
    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) = 0;

    template <typename T> static T *clone(const T *obj, IAllocator &allocator, bool deep = false) {
        if (!obj) {
            return nullptr;
        }
        return static_cast<T *>(obj->clone(allocator, deep));
    }

    // template <typename T> void setField(T *&field, T *newValue, GenerationalAllocatorWithGC *gc)
    // {
    //     ObjectHeader *thisHeader = headerOf(this);

    //     if (newValue) {
    //         ObjectHeader *newHeader = headerOf(newValue);

    //         // 写屏障：如果老年代对象引用年轻代对象
    //         if (gc->inElderGenSpace(thisHeader) && gc->inYoungGenSpace(newHeader)) {
    //             gc->recordOldToYoungRef(this, newValue);
    //         }
    //     }

    //     field = newValue;
    // }
};

template <typename T, typename U> inline bool isOfSameCls(const T *a, const U *b) noexcept {
    if (!a || !b)
        return false;
    // 借助 vtable 指针判断是否为同一对象
    auto vptrA = *reinterpret_cast<void *const *>(a);
    auto vptrB = *reinterpret_cast<void *const *>(b);
    return vptrA == vptrB;
}

using slot_t = uint64_t;

template <typename T> slot_t toSlot(T value) {
    if constexpr (std::is_same_v<T, slot_t>) {
        return value;
    } else {
        static_assert(sizeof(T) <= sizeof(slot_t), "T too large for slot");
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

        slot_t slot_value{};
        std::memcpy(&slot_value, &value, sizeof(T));
        return slot_value;
    }
}

template <typename T> T fromSlot(slot_t slot_value) {
    if constexpr (std::is_same_v<T, slot_t>) {
        return slot_value;
    } else {
        static_assert(sizeof(T) <= sizeof(slot_t), "T too large for slot");
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

        T value{};
        std::memcpy(&value, &slot_value, sizeof(T));
        return value;
    }
}

using Int    = int32_t;
using Long   = int64_t;
using Float  = float;
using Double = double;
using Bool   = bool;
using Byte   = std::byte;

inline std::ostream &operator<<(std::ostream &os, const Object *obj) {
    if (obj) {
        obj->print(os);
    } else {
        os << "null";
    }
    return os;
}

inline void printSlot(std::ostream &os, const slot_t data, TypeCode t) {
    if (isGCTraced(t)) {
        os << reinterpret_cast<const Object *>(data);
    } else {
        // 非引用类型，根据 type code 输出
        switch (t) {
        case TypeCode::Int:
            os << static_cast<int32_t>(data);
            break;
        case TypeCode::Long:
            os << static_cast<int64_t>(data);
            break;
        case TypeCode::Float:
            os << static_cast<float>(data);
            break;
        case TypeCode::Double:
            os << static_cast<double>(data);
            break;
        case TypeCode::Bool:
            os << (static_cast<bool>(data) ? "true" : "false");
            break;
        case TypeCode::Byte:
            os << "0x" << std::hex << static_cast<uint32_t>(data) << std::dec;
            break;
        case TypeCode::Void:
            os << "null";
            break;
        default:
            os << std::format("<slot of type: {}>", typeCodeToString(t));
            break;
        }
    }
}

constexpr slot_t NullSlot = 0;
constexpr Object *NullRef = nullptr;
