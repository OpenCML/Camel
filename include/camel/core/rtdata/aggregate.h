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
 * The slot data of a real Struct/Tuple, from the header the class itself defines.
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

} // namespace camel::core::rtdata