// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __time_now__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __time_strftime__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __time_strptime__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getTimeOpsMap();
