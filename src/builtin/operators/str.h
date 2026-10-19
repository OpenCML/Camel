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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

/// Checks a format string against the types of the arguments it formats, with the same
/// formatting code the runtime uses (a dummy value of each type): what is wrong with it, or
/// nullopt. The compiler calls it for a constant format string (CallSpec::checkFormat).
std::optional<std::string> checkFormatPattern(
    std::string_view pattern, std::span<camel::core::type::Type *const> argumentTypes);

slot_t __format__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __join__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
