// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one operator of the laws module.
 *
 *   stop_gradient(x)   x, through which no gradient flows
 *
 * The module is otherwise Camel: `src/stdlib/laws.cml` holds the tangent and the cotangent of each
 * primitive, one function per name a rule binds on its left side. This is the exception, and it is
 * an exception of kind rather than of ownership -- a value that no derivative flows through cannot
 * be written as a Camel expression, because it has no arithmetic to write. What it needs is a
 * kernel, and a kernel is C++.
 *
 * The declaration is not here: `laws.cml` declares it, and the declaration is what a program
 * imports and what a diagnostic quotes (design principles 3.3 compares the two: the native
 * operator has to say the same thing about touches that the declaration does).
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>

namespace camel::laws {

std::unordered_map<std::string, operator_t> operatorKernels();
/// Publishes the traits, the resources and the derivative rule of the module's own operator.
void registerOperatorMetadata();

} // namespace camel::laws