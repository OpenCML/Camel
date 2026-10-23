// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>

/// The kernels of the native operators of the `array` module: the sorting family, the merge of
/// two sorted arrays, and the primitives `flatten`, `unique` and `argsort` the rest of
/// `stdlib/array.cml` builds on. The signatures are the declarations in `stdlib/array.cml`; this
/// map binds them to the C++ implementations by the part after `array:`.
std::unordered_map<std::string, operator_t> getArrayOpsMap();

/// Declares what the operators of the map are: every one of them is pure except the in-place
/// forms the compiler introduces for `xs = xs->sort`, which change their operand (design
/// principles 11.3), and the sorts may fault on an element type that is not a number.
void declareArrayOperatorFacts();