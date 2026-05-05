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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/slot.h"
#include "camel/core/type.h"
#include "camel/utils/brpred.h"

#include <bit>
#include <cstring> // for std::memcpy
#include <format>
#include <functional>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace camel::core::rtdata {

class Object;

struct RefTraceInfo {
    static constexpr size_t npos = std::numeric_limits<size_t>::max();

    const Object *owner                      = nullptr;
    const camel::core::type::Type *ownerType = nullptr;
    const camel::core::type::Type *slotType  = nullptr;
    std::string_view ownerKind               = {};
    std::string_view slotName                = {};
    size_t slotIndex                         = npos;

    bool hasSlotIndex() const { return slotIndex != npos; }

    std::string describe() const {
        std::string out;
        if (!ownerKind.empty()) {
            out += ownerKind;
        } else {
            out += "Object";
        }

        if (owner) {
            out += std::format("@{}", static_cast<const void *>(owner));
        }

        if (hasSlotIndex()) {
            out += std::format("[{}]", slotIndex);
        }
        if (!slotName.empty()) {
            out += ".";
            out += slotName;
        }
        if (slotType) {
            out += ":";
            out += slotType->toString();
        }
        return out;
    }
};

class Object {
  public:
    using RefRelocator =
        std::function<Object *(Object *, const camel::core::type::Type *, const RefTraceInfo &)>;

    virtual ~Object() = default;
    virtual bool
    equals(const Object *other, const camel::core::type::Type *type, bool deep = false) const = 0;
    virtual Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const                                                    = 0;
    virtual void print(std::ostream &os, const camel::core::type::Type *type) const = 0;
    virtual void onMoved()                                                          = 0;
    virtual void finalize() noexcept {}
    virtual void updateRefs(const RefRelocator &relocate, const camel::core::type::Type *type) = 0;

    template <typename T>
    static T *clone(
        const T *obj, camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) {
        if (!obj) {
            return nullptr;
        }
        return static_cast<T *>(obj->clone(allocator, type, deep));
    }
};

template <typename T, typename U> inline bool isOfSameCls(const T *a, const U *b) noexcept {
    if (!a || !b)
        return false;
    // Use the vtable pointer to determine whether the objects are of the same class.
    auto vptrA = *reinterpret_cast<void *const *>(a);
    auto vptrB = *reinterpret_cast<void *const *>(b);
    return vptrA == vptrB;
}

constexpr Object *NullRef = nullptr;

} // namespace camel::core::rtdata

namespace camel::core::mm {

void writeBarrier(
    camel::core::rtdata::Object *ownerObject, const camel::core::type::Type *ownerType,
    slot_t storedSlot, const camel::core::type::Type *storedType);

void writeBarrier(
    camel::core::rtdata::Object *ownerObject, const camel::core::type::Type *ownerType,
    camel::core::rtdata::Object *storedObject, const camel::core::type::Type *storedType);

} // namespace camel::core::mm

namespace camel::core::rtdata {

template <typename T> constexpr slot_t toSlot(const T &value) noexcept {
    static_assert(sizeof(T) <= sizeof(slot_t), "T too large for slot");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

    if constexpr (std::is_same_v<T, slot_t>) {
        return value;
    } else if constexpr (std::is_pointer_v<T>) {
        // Convert pointer -> integer before storing into slot_t.
        return static_cast<slot_t>(reinterpret_cast<std::uintptr_t>(value));
    } else {
        if constexpr (sizeof(T) == sizeof(slot_t)) {
            return std::bit_cast<slot_t>(value);
        } else {
            // For values smaller than slot_t: keep the low bytes and zero the rest.
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

/** Safe slot printing without dereferencing Object*; used by debug_trace and similar paths that
 * may
 * observe uninitialized values. */
inline void printSlotSafe(std::ostream &os, const slot_t data, camel::core::type::Type *t) {
    if (t->isGCTraced()) {
        if (data == NullSlot) {
            os << "null";
            return;
        }
        os << "<" << t->toString() << " at 0x" << std::hex << data << std::dec << ">";
        return;
    }
    // Non-reference types follow printSlot.
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
    } else if (t->isOtherType()) {
        os << "<" << t->toString() << ">";
    } else {
        // Non-reference types: print according to the type code.
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
