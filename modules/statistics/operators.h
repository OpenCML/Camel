// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __statistics_mean__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __statistics_stdev__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getStatisticsOpsMap();
