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
 * Created: Sep. 25, 2025
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"
#include "core/type/type.h"

void __len_str__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __len_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __zip__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __head_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __tail_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __range__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __slice_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __concat_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __append_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __extend_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __contains_arr__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
