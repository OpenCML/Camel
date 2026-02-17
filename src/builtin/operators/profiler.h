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
 * Created: Oct. 01, 2025
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"

slot_t __profiler_begin__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __profiler_end__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __profiler_instant__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __profiler_enable__(ArgsView &with, ArgsView &norm, Context &ctx);
