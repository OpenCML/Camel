/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 20, 2026
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include "type.h"
#include <string>
#include <unordered_map>

// 算子实现
slot_t __python_py_call__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_py_exec__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_py_eval__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_py_run__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_py_attr__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_py_import__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_to_py__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_from_py__(ArgsView &with, ArgsView &norm, Context &ctx);

/** 供 python 协议 executor 使用的 URI -> operator_t 映射（key 为 suffix，如 "eval", "to_py"）。 */
std::unordered_map<std::string, operator_t> getPythonOpsMap();
