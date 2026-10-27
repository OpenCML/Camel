// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __rand_seed__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_next_key__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_philox__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_uniform_array__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_normal_array__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getRandomOpsMap();
