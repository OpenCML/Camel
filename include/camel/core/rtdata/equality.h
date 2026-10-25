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

/// Declares a type of a module whose values the core compares by content although it does not
/// know the type's representation: an array of numbers whose element type its values carry.
/// The module that declares such a type registers it where it registers the type (the tensor
/// module registers `Tensor`), and `==` then has one meaning for it as for any array (design
/// principles 4.2): the same shape, and elementwise equality everywhere.
///
/// The one type this is for is the transition period's `Tensor`: the numeric array whose element
/// type the type does not fix yet. It goes when a tensor is an array -- a tensor then carries its
/// element type, `==` reaches the array's own comparison without being told, and this registry is
/// deleted together with the marking that made it necessary. No other type belongs here: the
/// values of a module's type are not an array of numbers unless they are, and one registered here
/// would be compared as one.
void registerContentComparableType(camel::core::type::TypeCode code);

/// Whether `code` was declared that way. The operator that resolves `==` asks this to accept
/// the type, and the runtime reaches the type's own comparison through the value.
bool isRegisteredContentComparableType(camel::core::type::TypeCode code);

/// `==` for one pair of values of `type`.
bool valuesEqualByContent(slot_t a, slot_t b, const camel::core::type::Type *type);

} // namespace camel::core::rtdata