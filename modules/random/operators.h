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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __rand_seed__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_rand__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_randn__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_randint__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_choice__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_sample__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_shuffle__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getRandomOpsMap();
