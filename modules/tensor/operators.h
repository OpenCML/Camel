/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ctx = camel::core::context;

slot_t __tensor_add__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_subtract__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_multiply__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_divide__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_pow__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_matmul__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_idx__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_idx2d__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_lt__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_le__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_gt__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_ge__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_eq__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_empty__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_zeros__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_ones__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_full__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_random__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_range__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_eye__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_shape__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_sum__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_sum_axis__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_max_axis__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_argmax_axis__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_exp__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_log__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_transpose__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_concat__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_reshape__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __tensor_show__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getTensorOpsMap();
const std::vector<oper_group_ptr_t> &getTensorOperatorGroups();
