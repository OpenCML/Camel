// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The slot data of a real Struct/Tuple, from the header the class itself defines, and the
 * store-side materialisation that keeps a view out of a value that survives the element read.
 *
 * The array store path bulk-copies a slot-shaped compact element into a materialised Struct or
 * Tuple. It used to compute the offset of `data_` by hand (`sizeof(void *) + 2 * sizeof(uint32_t)`),
 * which silently encoded the header layout in a second place. Each class now states where its
 * slot data begins (`slotDataOffset()`), and this file only checks that the two agree: a change
 * to either header fails the static assertion instead of corrupting an element.
 */

#pragma once

#include "struct.h"
#include "tuple.h"
#include "view.h"

namespace camel::core::rtdata {

static_assert(
    Struct::slotDataOffset() == Tuple::slotDataOffset(),
    "Struct and Tuple slot data must start at the same offset: one bulk copy path serves both.");

/// The slot data of a non-view Struct or Tuple value, at the offset the class declares.
inline const slot_t *aggregateSlots(slot_t value) {
    const auto *object = fromSlot<const Object *>(value);
    return reinterpret_cast<const slot_t *>(
        reinterpret_cast<const std::byte *>(object) + Struct::slotDataOffset());
}

/// The mutable slot data of a non-view Struct or Tuple value.
inline slot_t *aggregateSlotsMut(slot_t value) {
    const auto *object = fromSlot<const Object *>(value);
    return reinterpret_cast<slot_t *>(
        const_cast<std::byte *>(reinterpret_cast<const std::byte *>(object)) +
        Struct::slotDataOffset());
}

/**
 * Whether `value`, whose static type has code `code`, is a compact element view. Inline and
 * reading the view flag of the header directly, because it is the test in front of every store
 * that can carry a view: a store of a real Struct or Tuple pays two loads, a scalar slot never
 * reaches it, and only a view reaches materializeIfView (H73 -- a frame slot is such a store).
 */
inline bool isViewValue(slot_t value, type::TypeCode code) {
    if (value == NullSlot) {
        return false;
    }
    if (code == type::TypeCode::Struct) {
        const auto *object = fromSlot<const Struct *>(value);
        return object != nullptr && object->isView();
    }
    if (code == type::TypeCode::Tuple) {
        const auto *object = fromSlot<const Tuple *>(value);
        return object != nullptr && object->isView();
    }
    return false;
}

/**
 * Materialises a view that is about to be stored where it outlives the element read that
 * produced it (H29). A view is tied to its read -- a slot-shaped element is read through one
 * reused thread-local cell, every other one through a ring -- so a value kept across the next
 * read names the wrong bytes. The store is the boundary: `reduce`'s accumulator, a loop's state.
 *
 * A callback result that is stored immediately does not need this: `map`'s output store copies
 * the element bytes and `filter`'s predicate is a boolean, so the view dies with the round. Only
 * a value that survives goes through here.
 *
 * This is inline and reads the view flag directly, because it runs once per element on the
 * surviving path: a scalar result pays one type comparison, a real Struct/Tuple two loads, and
 * only a view allocates (in the auto space, one real Struct/Tuple).
 */
inline slot_t materializeCallResult(slot_t value, const type::Type *exitType) {
    if (exitType == nullptr) {
        return value;
    }
    const type::TypeCode code = exitType->code();
    if (code == type::TypeCode::Struct) {
        const auto *object = fromSlot<const Struct *>(value);
        if (object == nullptr || !object->isView()) {
            return value;
        }
    } else if (code == type::TypeCode::Tuple) {
        const auto *object = fromSlot<const Tuple *>(value);
        if (object == nullptr || !object->isView()) {
            return value;
        }
    } else {
        return value;
    }
    return materializeIfView(value, exitType);
}

} // namespace camel::core::rtdata
