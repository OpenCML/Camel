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
 * Optimizers over parameter trees.
 *
 * A model is an ordinary value: a tensor, a float, or a struct / tuple of
 * them (nested to any depth, with non-trainable members such as integers
 * allowed). Its gradient, as returned by autodiff.grad, has the tangent type
 * of the model: the same shape, holding the trainable members only. The
 * optimizers map a model and its gradient to the updated model, leaving the
 * non-trainable members as they are:
 *
 *   sgd(params, grads, lr)                      => params
 *   adam_state(params)                          => OptimizerState
 *   adam(params, grads, state, lr)              => (params, OptimizerState)
 *
 * Updates are functional: they return new tensors rather than writing into
 * the old ones, so a model value can be shared freely.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::nn {

const std::vector<oper_group_ptr_t> &optimizerOperatorGroups();
std::unordered_map<std::string, operator_t> optimizerKernels();

} // namespace camel::nn
