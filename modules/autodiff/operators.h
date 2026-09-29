/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Operators of the autodiff module.
 *
 *   grad(f)            <W...>(N...) => gradient      (macro)
 *   value_and_grad(f)  <W...>(N...) => (value, gradient)  (macro)
 *   stop_gradient(x)   x, through which no gradient flows
 *   @vjp<rule> func f  f with a custom derivative rule (see rules.h)
 *
 * grad and value_and_grad are macro operators: with a static f, std::macro
 * builds the gradient graph at compile time and later passes see it as
 * ordinary code; otherwise they build it when they run.
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
