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
 * Updated: Mar. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __os_exit__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_sleep__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_whoami__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_cpu_count__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_set_terminal_raw_mode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_has_input__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_get_char__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_get_chars__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __os_clear_input_buffer__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getOSOpsMap();
