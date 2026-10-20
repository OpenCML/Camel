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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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