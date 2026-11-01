// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `reinterp<vmap>(f)`: `vmap(f)` as a native operator of the `transforms`
 * module.
 *
 * Same split as `jvp`'s (jvp_operator.h): a resolver for the type of `vmap(f)` and a kernel that
 * builds the batched graph when the call runs. The lifted type is `Vmap<F>` = the argument's
 * function type with one dimension of unknown length in front of every parameter and of the result
 * (`Lift<T> = RowsOf<T>`), which is what `vmapSignature` (vmap.h) computes.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

/// The operator groups of the batching transform (`transforms:vmap`), beside the module's.
const std::vector<oper_group_ptr_t> &vmapOperatorGroups();
std::unordered_map<std::string, operator_t> vmapOperatorKernels();
/// Publishes the purity and the graph-reading traits of `vmap`.
void registerVmapOperatorMetadata();

} // namespace camel::transforms