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
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include <string>
#include <unordered_map>


/** 模块导出的自定义类型 PyObject，表示 Python 对象句柄。由 python 模块 exportType 导出。 */
Type *getPyObjectType();

// 算子实现
slot_t __python_eval__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_to_py__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_from_py_int__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_from_py_float__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_from_py_bool__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __python_from_py_string__(ArgsView &with, ArgsView &norm, Context &ctx);

/** 供 python 协议 executor 使用的 URI -> operator_t 映射（key 为 suffix，如 "eval", "to_py"）。 */
std::unordered_map<std::string, operator_t> getPythonOpsMap();
