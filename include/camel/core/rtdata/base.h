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
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/slot.h"
#include "camel/core/type.h"
#include "camel/utils/brpred.h"

#include <cstring> // for std::memcpy
#include <functional>

namespace camel::core::rtdata {

class Object {
  public:
    virtual ~Object() = default;
    virtual bool
    equals(const Object *other, const camel::core::type::Type *type, bool deep = false) const = 0;
    virtual Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const                                                    = 0;
    virtual void print(std::ostream &os, const camel::core::type::Type *type) const = 0;
    virtual void onMoved()                                                          = 0;
    virtual void updateRefs(
        const std::function<Object *(Object *)> &relocate, const camel::core::type::Type *type) = 0;

    template <typename T>
    static T *clone(
        const T *obj, camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) {
        if (!obj) {
            return nullptr;
        }
        return static_cast<T *>(obj->clone(allocator, type, deep));
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

constexpr Object *NullRef = nullptr;

} // namespace camel::core::rtdata

namespace camel::core::rtdata {

template <typename T> constexpr slot_t toSlot(const T &value) noexcept {
    static_assert(sizeof(T) <= sizeof(slot_t), "T too large for slot");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

    if constexpr (std::is_same_v<T, slot_t>) {
        return value;
    } else if constexpr (std::is_pointer_v<T>) {
        // 指针 -> 整数再放入 slot_t
        return static_cast<slot_t>(reinterpret_cast<std::uintptr_t>(value));
    } else {
        if constexpr (sizeof(T) == sizeof(slot_t)) {
            return std::bit_cast<slot_t>(value);
        } else {
            // 小于 slot_t 的情况：低位保存，其他位补零
            slot_t tmp{};
            std::memcpy(&tmp, &value, sizeof(T));
            return tmp;
        }
    }
}

template <typename T> constexpr T fromSlot(slot_t slot_value) noexcept {
    static_assert(sizeof(T) <= sizeof(slot_t), "T too large for slot");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially_copyable");

    ASSERT(slot_value != DeadSlot, std::format("Accessing uninitialized slot"));

    if constexpr (std::is_same_v<T, slot_t>) {
        return slot_value;
    } else if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(static_cast<std::uintptr_t>(slot_value));
    } else {
        if constexpr (sizeof(T) == sizeof(slot_t)) {
            return std::bit_cast<T>(slot_value);
        } else {
            T tmp{};
            std::memcpy(&tmp, &slot_value, sizeof(T));
            return tmp;
        }
    }
}

using Int32   = int32_t;
using Int64   = int64_t;
using Int     = Int64;
using Float32 = float;
using Float64 = double;
using Float   = Float64;
using Bool    = bool;
using Byte    = std::byte;

/** 安全打印 slot，不访问 Object* 指针，用于 debug_trace 等可能读到未初始化值的场景 */
inline void printSlotSafe(std::ostream &os, const slot_t data, camel::core::type::Type *t) {
    if (t->isGCTraced()) {
        if (data == NullSlot) {
            os << "null";
            return;
        }
        os << "<" << t->toString() << " at 0x" << std::hex << data << std::dec << ">";
        return;
    }
    // 非引用类型与 printSlot 一致
    switch (t->code()) {
    case camel::core::type::TypeCode::Int32:
        os << fromSlot<Int32>(data);
        break;
    case camel::core::type::TypeCode::Int64:
        os << fromSlot<Int64>(data);
        break;
    case camel::core::type::TypeCode::Float32:
        os << fromSlot<Float32>(data);
        break;
    case camel::core::type::TypeCode::Float64:
        os << fromSlot<Float64>(data);
        break;
    case camel::core::type::TypeCode::Bool:
        os << (fromSlot<Bool>(data) ? "true" : "false");
        break;
    case camel::core::type::TypeCode::Byte:
        os << "0x" << std::hex << static_cast<uint64_t>(data) << std::dec;
        break;
    case camel::core::type::TypeCode::Void:
        os << "null";
        break;
    case camel::core::type::TypeCode::Ref:
        os << "ref";
        break;
    default:
        os << std::format("<{}>", t->toString());
        break;
    }
}

inline void printSlot(std::ostream &os, const slot_t data, camel::core::type::Type *t) {
    ASSERT(
        data != DeadSlot,
        std::format("Accessing uninitialized slot in printSlot: {}", t->toString()));
    // GCTraced types (including OtherTypes like Tensor) hold Object*: use object->print.
    // Check isGCTraced before isOtherType so Tensor/other Object-backed types print properly.
    if (t->isGCTraced()) {
        if (data == NullSlot) {
            os << "null";
            return;
        }
        reinterpret_cast<const Object *>(data)->print(os, t);
    } else {
        // 非引用类型，根据 type code 输出
        switch (t->code()) {
        case camel::core::type::TypeCode::Int32:
            os << fromSlot<Int32>(data);
            break;
        case camel::core::type::TypeCode::Int64:
            os << fromSlot<Int64>(data);
            break;
        case camel::core::type::TypeCode::Float32:
            os << fromSlot<Float32>(data);
            break;
        case camel::core::type::TypeCode::Float64:
            os << fromSlot<Float64>(data);
            break;
        case camel::core::type::TypeCode::Bool:
            os << (fromSlot<Bool>(data) ? "true" : "false");
            break;
        case camel::core::type::TypeCode::Byte:
            os << "0x" << std::hex << static_cast<uint64_t>(data) << std::dec;
            break;
        case camel::core::type::TypeCode::Void:
            os << "null";
            break;
        case camel::core::type::TypeCode::Ref:
            os << "ref";
            break;
        default:
            os << std::format("<{}>", t->toString());
            break;
        }
    }
}

} // namespace camel::core::rtdata
