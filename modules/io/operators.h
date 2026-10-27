// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// The terminal operators, implemented in operators.cpp.
slot_t __io_set_terminal_raw_mode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_has_input__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_get_chars__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_clear_input_buffer__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __io_input__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getIOOpsMap();