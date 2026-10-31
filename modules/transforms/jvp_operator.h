// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `reinterp<jvp>(f)`: `jvp(f)` as a native operator of the `transforms`
 * module.
 *
 * The transform is a value-level one, not a pass (jvp.h says why: a `RuntimeGraphRewritePass` has
 * to answer with the program it was given, and this transform answers with a new function). Its
 * two halves are therefore the ones `camel::autodiff`'s `grad` has:
 *
 *   `JvpResolver`  the type of `jvp(f)`: the forward transform of the function type it is given;
 *   `jvpKernel`    a call of `jvp(f)` at run time: builds the new graph and answers with a
 *                  function value naming it.
 *
 * The resolver's type is `jvpSignature(f)` (jvp.h) wrapped as `(f: F) => lifted`, so
 * `jvp(f)(2.0, 1.0)` type-checks as calling a function of two floats whose result is the pair
 * `(value, tangent)`.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

const std::vector<oper_group_ptr_t> &operatorGroups();
std::unordered_map<std::string, operator_t> operatorKernels();
/// Publishes the purity and the graph-reading traits of the module's own operators.
void registerOperatorMetadata();

} // namespace camel::transforms