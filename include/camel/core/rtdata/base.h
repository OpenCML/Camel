// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/utils/number_text.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/half.h"
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
#include <typeinfo>

namespace camel::core::mm {
class Block;
}

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
    /// The data block this value holds (core/mm/block.h), or null when its data is inline or in
    /// the collected heap. The collector reads it when it promotes a value and when it measures
    /// the elder live base: a block held by an elder value can be released only by a major
    /// collection, and a block reached through several views is counted once, by its identity
    /// (gen.cpp, forward / sweepOldGen; mm/alloc/allocator.h, noteBlockHeld).
    virtual camel::core::mm::Block *heldBlock() const noexcept { return nullptr; }
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
    // Fast path: the same vtable means the same class.
    auto vptrA = *reinterpret_cast<void *const *>(a);
    auto vptrB = *reinterpret_cast<void *const *>(b);
    if (vptrA == vptrB) {
        return true;
    }
    // Modules are loaded with RTLD_LOCAL, and a class whose virtual functions are all inline
    // (no key function, e.g. String) gets a private vtable copy in each module that constructs
    // it. Different vtables therefore do not prove different classes; compare dynamic types.
    return typeid(*a) == typeid(*b);
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

/// The text of a bf16 or an f16 slot, by the one float text rule (camel/core/rtdata/half.h,
/// `halfText`): the shortest text that reads back as the same element of the type. The slot holds
/// the type's 16-bit pattern in the low half; `code` says which of the two types it is. A path that
/// has a slot and its type calls this rather than widening to f32 first, or the value would print
/// by f32's rule (`0.100097656` for bf16 `0.1`).
inline std::string halfValueText(slot_t data, camel::core::type::TypeCode code) {
    const uint16_t bits = camel::core::rtdata::halfBitsOfSlot(data);
    return code == camel::core::type::TypeCode::BFloat16
               ? camel::core::rtdata::halfText(camel::core::rtdata::BFloat16::fromBits(bits))
               : camel::core::rtdata::halfText(camel::core::rtdata::Float16::fromBits(bits));
}

/// The f32 a bf16 or an f16 slot widens to, exactly. A path that formats the value through a
/// library (`format` hands floats to fmt) widens here and keeps `code` to say whose text a plain
/// placeholder writes (src/builtin/operators/float_format.h).
inline float halfValueFloat(slot_t data, camel::core::type::TypeCode code) {
    return code == camel::core::type::TypeCode::Float16
               ? camel::core::rtdata::halfToFloat<camel::core::rtdata::Float16>(data)
               : camel::core::rtdata::halfToFloat<camel::core::rtdata::BFloat16>(data);
}

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
        os << camel::utils::floatText(fromSlot<Float32>(data));
        break;
    case camel::core::type::TypeCode::Float64:
        os << camel::utils::floatText(fromSlot<Float64>(data));
        break;
    case camel::core::type::TypeCode::BFloat16:
    case camel::core::type::TypeCode::Float16:
        // The same text `printSlot` writes for the element (camel/core/rtdata/half.h): this path
        // must agree with it, or a frame log would print a value two ways.
        os << halfValueText(data, t->code());
        break;
    case camel::core::type::TypeCode::Bool:
        os << (fromSlot<Bool>(data) ? "true" : "false");
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

/// Prints one value (rtdata/print.cpp). The composites (array, tuple, struct, enum, map and set)
/// are walked with an explicit stack there, so the depth of the data does not consume the native
/// stack; scalars and the remaining object-backed types are printed directly.
void printValue(std::ostream &os, slot_t data, camel::core::type::Type *t);

/// The text of a value with a bound on arrays: an array with more than `arrayLimit` elements
/// prints as its type, address and length instead of its elements, at every depth of the value.
/// The frame log uses it so that enabling the Frame category on a program that builds a large
/// collection does not print that collection element by element on every read.
void printSlotCapped(
    std::ostream &os, slot_t data, camel::core::type::Type *t, size_t arrayLimit);

/// The text of a value as `println`, `string(x)` and a `{}` of `format` write it. A float has one
/// text, the shortest that reads back as the same number (camel::utils::floatText; design
/// principles 7.7), also as an element of an array, a tuple, a struct, a map or a set; for a bf16
/// or an f16 "the same number" is an element of that type (`halfValueText`, half.h).
inline void printSlot(std::ostream &os, const slot_t data, camel::core::type::Type *t) {
    printValue(os, data, t);
}

} // namespace camel::core::rtdata
