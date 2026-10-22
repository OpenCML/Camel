// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __math_abs__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_exp__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_round__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_ceil__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_floor__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_bin__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_oct__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_hex__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_sqrt__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_re__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_im__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_conj__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getMathOpsMap();
