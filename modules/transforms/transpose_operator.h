// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `transpose(f)` as an operator of the `transforms` module (see transpose.h).
 *
 * The wiring is `jvp`'s, copied rather than shared: a type resolver, a kernel and a registration.
 * The work is `buildTransposeGraph` (transpose.cpp), which is the one place that knows what
 * `transpose(f)` is. Like `jvp`, it answers with a function value naming the graph it built, so `f`
 * itself is untouched -- the author's ruling (a824031aa).
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/rtdata/func.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

/// The operator groups this module adds to `transforms:transpose`.
const std::vector<oper_group_ptr_t> &transposeOperatorGroups();

/// The kernels of those groups, merged into the module's table by `operatorKernels`.
std::unordered_map<std::string, operator_t> transposeOperatorKernels();

} // namespace camel::transforms