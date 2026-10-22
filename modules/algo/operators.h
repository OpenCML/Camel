// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __algo_insert_sort__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __algo_quick_sort__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __algo_merge_sort__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __algo_merge_sorted_arrays__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getAlgoOpsMap();
