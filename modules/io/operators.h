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
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// The terminal operators, implemented in operators.cpp.
slot_t __io_set_terminal_raw_mode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_has_input__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_get_char__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_get_chars__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_clear_input_buffer__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getIOOpsMap();