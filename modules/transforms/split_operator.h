// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `split`: `split(f)` as a native operator of the `transforms` module.
 *
 * Same split as `vmap`'s (vmap_operator.h): a resolver for the type of `split(f)` and a kernel that
 * builds the two graphs when the call runs. What the call answers with is a **pair of functions**,
 * `(forward, backward)`, so the resolver answers a tuple type, not a function type: the halves have
 * different parameter lists, and only a tuple can hold both.
 *
 * `split(f)(3.0)` is not a call: the call is `split(f)`, and the pair it answers with is
 * destructured by the caller (`let (fwd, lin) = split(f)`), which is what the plan's cases write.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

/// The operator group of the split step (`transforms:split`), beside the module's others.
const std::vector<oper_group_ptr_t> &splitOperatorGroups();
std::unordered_map<std::string, operator_t> splitOperatorKernels();
/// Publishes the purity and the graph-reading traits of `split`.
void registerSplitOperatorMetadata();

/// The reading of one `split(f)` call: which function the two graphs were built from.
void printSplitCallReading(std::ostream &os, std::string_view function);

} // namespace camel::transforms