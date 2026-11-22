// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Equality by content (design principles 4.2): `==` compares arrays, tuples, structs and
 * enums part by part, not by the identity of the object. The runtime objects of module types
 * (Map, Set, Enum) use these functions too, so they live in the core rather than beside the
 * builtin operators.
 */

#pragma once

#include "camel/core/type.h"

namespace camel::core::rtdata {

/// Whether values of `type` can be compared with `==`: primitives, and arrays, tuples,
/// structs, maps, sets and enums whose parts all can. A recursive enum is comparable when
/// what it is built from is (`Tree<int>` is: its payload is an int and itself).
bool isContentComparable(const camel::core::type::Type *type);

/// `==` for one pair of values of `type`.
bool valuesEqualByContent(slot_t a, slot_t b, const camel::core::type::Type *type);

} // namespace camel::core::rtdata