// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include <string_view>

namespace camel::core::type {
struct DataLayout;
}

namespace camel::core::rtdata {

class Object;

/// A complex number read from the compact bytes of an element. Declared here rather than
/// inline because making the box allocates, and the leaf read of every other type does not.
slot_t readPureComplex(const std::byte *bytes, camel::core::type::TypeCode code);

/// The view of one compact pure-data aggregate element at `bytes`; `stride` is the element
/// size of the array the element came from (the layout size for a nested field). Returns the
/// view as a slot.
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
    case camel::core::type::TypeCode::Bool: {
        const uint8_t value = *reinterpret_cast<const uint8_t *>(bytes);
        return toSlot(value);
    }
    case camel::core::type::TypeCode::Complex64:
    case camel::core::type::TypeCode::Complex128:
        return readPureComplex(bytes, code);
    default:
        return NullSlot;
    }
}

/**
 * Whether `slot`, whose static type is `type`, is a compact element view: a Struct/Tuple whose
 * size field carries the view flag. This check is always compiled: it is the detector behind
 * the debug-only store guard and the block/view self-tests.
 */
bool isViewSlot(slot_t slot, const camel::core::type::Type *type);

/**
 * The debug-only guard at a store boundary: a view must have been materialised before a value
 * is stored (design principles 10.5). Compiled out in release builds; the
 * always-compiled detector above is what the self-test exercises.
 */
void assertStoredValueIsNotView(
    slot_t slot, const camel::core::type::Type *type, std::string_view where);

#if defined(NDEBUG)
#define CAMEL_ASSERT_NOT_VIEW(slot, type, where) ((void)0)
#else
#define CAMEL_ASSERT_NOT_VIEW(slot, type, where)                                             \
    ::camel::core::rtdata::assertStoredValueIsNotView((slot), (type), (where))
#endif

/// Materialises a view into a real Struct/Tuple, and returns any other slot unchanged. Only a
/// compact pure-data aggregate can be a view; the value is allocated in the auto space.
slot_t materializeIfView(slot_t value, const camel::core::type::Type *type);

/// Materialises the compact bytes of `type` into a real Struct/Tuple.
Object *materializeAggregate(
    const std::byte *bytes, const camel::core::type::Type *type,
    camel::core::mm::IAllocator &allocator);

} // namespace camel::core::rtdata