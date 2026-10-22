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
slot_t __rand_rand__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_randn__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_randint__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_choice__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_sample__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_shuffle__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_key__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_split__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __rand_permutation__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getRandomOpsMap();
