// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Run-time side of the two tagged forms `T?` and `T!` (design principles 3.8; layout in
 * docs/technical/29_nullable_and_fallible_values.md).
 *
 * A value of either form is an ordinary Tuple object (or the null slot); which form a slot
 * holds is known from its static type only. These helpers are the one place that knows the
 * layout:
 *
 *   T?   null slot                       null
 *        Tuple(1) { value }              a value
 *   T!   Tuple(2) { value, null }        a value
 *        Tuple(2) { zero, Error* }       failed
 *
 * An operator written in C++ that can fail declares its result `TupleType::fallible(T)` and
 * returns makeOk(...) or makeError(...).
 */

#pragma once

#include "camel/core/rtdata/base.h"
#include "camel/core/type/composite/tuple.h"

#include <ostream>
#include <string_view>

namespace camel::core::rtdata {

/// Tag of a `T?`.
inline bool isNullValue(slot_t nullable) { return nullable == NullSlot; }
/// The value of a `T?`; the zero slot when it is null.
slot_t nullablePayload(slot_t nullable);
/// A `T?` holding `value`.
slot_t makeSome(slot_t value, const type::TupleType *nullableType);

/// Tag of a `T!`.
bool isErrorValue(slot_t fallible);
/// The value of a `T!`; the zero slot when it has failed.
slot_t falliblePayload(slot_t fallible);
/// The Error structure of a `T!`; the null slot when it has not failed.
slot_t fallibleError(slot_t fallible);
/// A `T!` holding `value`.
slot_t makeOk(slot_t value, const type::TupleType *fallibleType);
/// A failed `T!` carrying the Error structure `error` (type::errorType()).
slot_t makeFailed(slot_t error);
/// A new Error structure.
slot_t
makeErrorInfo(std::string_view kind, std::string_view message, std::string_view location);
/// A failed `T!` with a new Error structure.
slot_t makeError(std::string_view kind, std::string_view message, std::string_view location);

/// Prints a tagged value: `null`, the value, or `error <kind>: <message>`.
void printTaggedSlots(std::ostream &os, const slot_t *slots, const type::TupleType *type);

} // namespace camel::core::rtdata
