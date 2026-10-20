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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The `set` operators a path assignment lowers to (design principles 3.5): `v.f = e` is
 * `v = set_field(v, f, e)`, `v[i] = e` is `v = set_index(v, i, e)`. Both are ordinary pure
 * operators that return the value with one place changed; the in-place form of the index set
 * writes the array it is given and returns it, and only the compiler names it (the lowering
 * hands it the result of :var/take). The lowering itself is Builder::pathAssignmentOf.
 */

#pragma once

#include "camel/core/operator.h"

#include <vector>

/// `set_field(aggregate, field, value)`: the same struct or tuple with the field at `field`
/// (a compile-time position) replaced. Pure: the argument is unchanged.
slot_t __builtin__set_field__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
/// `set_index(array, index, value)`: a new array with the element at `index` replaced. Pure.
slot_t __builtin__set_index__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
/// The in-place form of :op/set_index: the array may be written and is returned.
slot_t __builtin__set_index_in_place__(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/// The operator group of the three operators, exported by the core module (globals.cpp).
const std::vector<oper_group_ptr_t> &getSetValueOperatorGroups();