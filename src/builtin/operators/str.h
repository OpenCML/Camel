// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
