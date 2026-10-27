// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include "type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __python_exec__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_eval__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_run_file__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_import_module__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_attr__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_call__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_to_string__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_wrap__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_upwrap__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

/** 供 python 协议 executor 使用的 URI -> operator_t 映射（key 为 suffix，如 "eval", "wrap"）。 */
std::unordered_map<std::string, operator_t> getPythonOpsMap();
