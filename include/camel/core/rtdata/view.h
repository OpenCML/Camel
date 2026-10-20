/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * A compact element view: the value an array consumer reads without materialising a Struct or
 * Tuple. The element bytes live in the array's block (stable across a collection), and the
 * view only carries a pointer to them, the element layout and the element type. Field access
 * reads through the layout; a nested aggregate field becomes another view.
 *
 * A view is not a Camel value: it is tied to the thread-local view ring and must not be stored.
 * Store paths (Struct::set, Tuple::set) and the operation that actually needs a value
 * materialise it first. The ring makes the read path allocation-free; the block makes it safe
 * across the safepoints in a callback.
 */

#pragma once

#include "base.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/slot.h"
#include "camel/core/type/base.h"

#include <cstddef>
#include <cstring>

namespace camel::core::type {
struct DataLayout;
}

namespace camel::core::rtdata {

class Object;

/// The view of one compact pure-data aggregate element at `bytes`; `stride` is the element
/// size of the array the element came from (the layout size for a nested field). Returns the
/// view as a slot.
/// The slot data of a real (non-view) Struct/Tuple. Both headers are one pointer plus two
/// uint32 fields, so data begins after that; this lets the array store path bulk-copy a
/// slot-shaped element without knowing which of the two classes it has.
inline const slot_t *aggregateSlots(slot_t value) {
    return reinterpret_cast<const slot_t *>(
        reinterpret_cast<const std::byte *>(fromSlot<const void *>(value)) +
        sizeof(void *) + 2 * sizeof(uint32_t));
}

/// Whether a Struct/Tuple slot is a compact-element view (high bit of its size field).
inline bool isAggregateView(slot_t value) {
    const auto *bytes = reinterpret_cast<const std::byte *>(fromSlot<const void *>(value));
    uint32_t size     = 0;
    std::memcpy(&size, bytes + sizeof(void *), sizeof(size));
    return (size & 0x80000000u) != 0;
}
slot_t pureViewOf(
    const std::byte *bytes, const camel::core::type::Type *type,
    const camel::core::type::DataLayout *layout, size_t stride);

/// One pure-data leaf, read from the compact bytes. Inline: field access in a compact-element
/// view is the hot path of map/filter/reduce over struct and tuple arrays.
inline slot_t readPureScalar(const std::byte *bytes, camel::core::type::TypeCode code) {
    switch (code) {
    case camel::core::type::TypeCode::Int32: {
        int32_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return toSlot(value);
    }
    case camel::core::type::TypeCode::Int64: {
        int64_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return toSlot(value);
    }
    case camel::core::type::TypeCode::Float32: {
        float value = 0.0f;
        std::memcpy(&value, bytes, sizeof(value));
        return toSlot(value);
    }
    case camel::core::type::TypeCode::Float64: {
        double value = 0.0;
        std::memcpy(&value, bytes, sizeof(value));
        return toSlot(value);
    }
    case camel::core::type::TypeCode::Bool:
    case camel::core::type::TypeCode::Byte: {
        const uint8_t value = *reinterpret_cast<const uint8_t *>(bytes);
        return toSlot(value);
    }
    default:
        return NullSlot;
    }
}

/// Materialises a view into a real Struct/Tuple, and returns any other slot unchanged. Only a
/// compact pure-data aggregate can be a view; the value is allocated in the auto space.
slot_t materializeIfView(slot_t value, const camel::core::type::Type *type);

/// Materialises the compact bytes of `type` into a real Struct/Tuple.
Object *materializeAggregate(
    const std::byte *bytes, const camel::core::type::Type *type,
    camel::core::mm::IAllocator &allocator);

} // namespace camel::core::rtdata