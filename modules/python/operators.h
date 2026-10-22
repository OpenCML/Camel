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
slot_t __python_py_call__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_call_kw__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_exec__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_eval__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_run__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_attr__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_import__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_print__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_println__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_wrap__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_upwrap__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __python_py_dispose__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

/** 供 python 协议 executor 使用的 URI -> operator_t 映射（key 为 suffix，如 "eval", "wrap"）。 */
std::unordered_map<std::string, operator_t> getPythonOpsMap();
