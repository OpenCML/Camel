/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Neural-network graph-generation operators.
 *
 * `apply_gradients` is intentionally narrow in this demo: it accepts a static
 * scalar loss function and emits a linear-MSE SGD step graph.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ctx = camel::core::context;

slot_t __nn_apply_gradients__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_compile_step__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_vjp__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_parameter__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_value__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_grad__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_zero_grad__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_add_grad__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __nn_sgd__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getNnOpsMap();
const std::vector<oper_group_ptr_t> &getNnOperatorGroups();
