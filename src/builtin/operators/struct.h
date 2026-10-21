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
 * Author: Zhenjie Wei
 * Created: Sep. 25, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

slot_t __len_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __iter_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __len_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __zip__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __head_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tail_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __range__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __slice_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The subscript forms of design principles 5.4: `t[a:b:s]`, `t[idx]` and `last(t)`.
slot_t __slice_sub__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __idx_take_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __last_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __concat_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __append_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// A private copy of an array: the first state of a loop whose body only appends to it.
slot_t __copy_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The loop-state form of `append`: may write into the array's own block.
slot_t __append_owned_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The in-place form of `append` (OperatorTable::setInPlaceForm; Builder::inPlaceRebindingOf):
// the array `:var/take` handed it is appended to and returned, or a copy when its storage
// cannot be written in place.
slot_t __append_in_place_arr__(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __extend_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __contains_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __contains_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

// Tangent arrays (design principles 9.9): the tangent of an array is the array of the tangents
// of its elements. Inserted by autodiff into gradient graphs, never written by a program.
slot_t __tangent_add_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_zeros_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_scatter_add_arr__(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_init_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_last_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
