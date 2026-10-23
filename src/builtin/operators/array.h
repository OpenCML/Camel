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
/// principles 11.3), and the sorts may fault on an element type that is not a number.
void declareArrayOperatorFacts();

// The reductions' kernels, in builtin/operators/array_reduce.cpp: one executor per element type
// the rule of the kernel declaration in stdlib/array.cml admits. The declaration's own `when` is
// what decides which calls its executor runs (`Builder::declaredRuleApplies`), so an element type
// the condition refuses has no executor and runs the Camel body, and the executor of an admitted
// type runs every call of that type (design principles 5.5, 10.4; H228: one operator, per-type
// executors).
slot_t __array_sum__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_min__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __array_max__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);