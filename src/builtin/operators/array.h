// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>

/// The kernels of the native operators of the `array` module: the sorting family, the merge of
/// two sorted arrays, the primitives `flatten`, `unique` and `argsort` the rest of
/// `stdlib/array.cml` builds on, and the reductions `sum`, `min` and `max`
/// (builtin/operators/array_reduce.cpp). The signatures are the declarations in
/// `stdlib/array.cml`; this map binds them to the C++ implementations by the part after `array:`.
std::unordered_map<std::string, operator_t> getArrayOpsMap();

/// Declares what the operators of the map are: every one of them is pure except the in-place
/// forms the compiler introduces for `xs = xs->sort`, which change their operand (design
/// principles 11.3). One entry per operator, and one operator per element type: the sorts and the
/// reductions both name the element type they take in their own name (`sort_i`, `sum_d`, ...), so
/// none of them asks an array which element type it holds and none of them faults on a type it
/// does not take — such a call has no declaration to compile through at all.
void declareArrayOperatorFacts();

// The reductions' kernels, in builtin/operators/array_reduce.cpp: one executor per element type,
// named after the element type it takes, as `math:min_i` names its own (`_i` i32, `_l` int, `_f`
// f32, `_d` float, `_bf` bf16, `_f6` f16, `_b` bool, `_s` string). Each body-less declaration in
// stdlib/array.cml names one of them with its `#op` tag and admits exactly that element type with
// its own `when` (`Builder::declaredRuleApplies`), so the element type is known where the executor
// is chosen: an element type no declaration admits has no executor and runs the Camel body, and the
// executor of an admitted type runs every call of that type (design principles 5.5, 10.4; one
// operator, per-type executors).

// `sum`, one per element type of `is_numeric` that has an executor:
slot_t __array_sum_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_sum_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_sum_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_sum_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_sum_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_sum_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

// `min`, one per element type of `is_ordered` that has an executor:
slot_t __array_min_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_b__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

// `max`, the same element types:
slot_t __array_max_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_b__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The layout atoms and the reductions along an axis (builtin/operators/array_axis.cpp): generic
// declarations of stdlib/array.cml, one executor each, reading the element type of the array.
slot_t __array_shape__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_numel__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_dim__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_elements__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_to_bytes__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_sum_axis__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_mean_axis__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min_axis__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max_axis__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_argmax_axis__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_argmin_axis__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
