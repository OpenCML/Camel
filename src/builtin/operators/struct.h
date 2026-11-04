// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"

#include <memory>

slot_t __len_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __iter_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __len_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
/// `extent_of::<A>()`: the extents the array type `A` writes down, as an `int[]`. The value is a
/// fact of the type argument and is settled where the call is built and where the name is used as a
/// value (`ExtentOfResolver::valueFromTypeArgs`), so this kernel is the case of a call that reached
/// the run without being settled -- a defect of the compiler, never a program's answer.
slot_t __op_extent_of__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
/// The rule of `extent_of::<A>()`: it reads the call's type argument and answers `int[]` for an
/// array type whose extents are all written down (`camel::core::type::statedExtentsOf`), and it
/// answers the list itself as its value (`valueFromTypeArgs`). A type that leaves an extent open is
/// refused where the call is instantiated, with `SemanticDiag::ExtentOfNeedsStatedShape`, and the
/// sentence names the position that has no number in it.
std::shared_ptr<camel::core::type::FuncTypeResolver> extentOfResolver();

slot_t __zip__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __range__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The subscript forms of design principles 5.4: `t[a:b:s]`, `t[i]` (on the axis it names, which
// is the first one when it names none), `t[idx]` and `last(t)`.
slot_t __slice_sub__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The array case of a position (`t[i]`), read by the same operator's dispatch when its value is an
// array (src/builtin/operators/ops.cpp): the string, tuple and struct positions are that
// dispatch's own cases.
slot_t __index_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __slice_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __slice_bytes__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __len_bytes__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __add_bytes__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __eq_bytes__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
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
slot_t __contains_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __contains_str__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

// Tangent arrays (design principles 9.9): the tangent of an array is the array of the tangents
// of its elements. Inserted by autodiff into gradient graphs, never written by a program.
slot_t __tangent_add_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_zeros_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The reverse of a position (`t[i]`): the gradient of what the position took -- one element, or
// the slab along the axis for a deeper array -- added back where it was taken from, the rest of
// the array left at its zero tangent. The axis is the fourth word, absent meaning the first.
slot_t __tangent_scatter_add_arr__(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The reverse of a slice: the gradient placed back in the range the slice took, zeros elsewhere.
slot_t __tangent_scatter_slice_arr__(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The reverse of a take (`t[idx]`): the gradient of the positions the indices name added back
// where they were taken from, the rest of the array left at its zero tangent. The axis is the
// fourth word, absent meaning the first.
slot_t __tangent_scatter_take_arr__(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_init_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __tangent_last_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
