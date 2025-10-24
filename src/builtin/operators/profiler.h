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
 * Created: Oct. 1, 2025
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/context/frame.h"
#include "core/operator.h"

void __profiler_begin__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __profiler_end__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __profiler_instant__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __profiler_enable__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
