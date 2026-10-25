// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Operators of the autodiff module.
 *
 *   grad(f)            <W...>(N...) => gradient of W0   (macro)
 *   with_grad(f)       <W...>(N...) => (value, gradient of W0)  (macro)
 *   stop_gradient(x)   x, through which no gradient flows
 *
 * A custom derivative rule is a declaration tag, not an operator of this module
 * (design principles 6.2, 8): `#vjp(rule)` before a function declaration attaches the
 * rule's graph to the function's graph (see rules.h).
 *
 * grad and with_grad differentiate the **first with parameter** of f (design
 * principles 9.9): the weights are `with` parameters, the ordinary parameters are
 * data, and the remaining `with` parameters are constants. They are macro operators:
 * with a static f, std::macro builds the gradient graph at compile time and later
 * passes see it as ordinary code; otherwise they build it when they run.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::autodiff {

const std::vector<oper_group_ptr_t> &operatorGroups();
std::unordered_map<std::string, operator_t> operatorKernels();
/// Publishes the derivative rules and traits of the module's own operators.
void registerOperatorMetadata();

} // namespace camel::autodiff
