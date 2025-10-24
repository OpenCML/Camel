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
 * Created: Jul. 29, 2025
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"
#include "core/type/type.h"

void __builtin__assn__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_add__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_sub__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_mul__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_div__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_mod__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_mat__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_pow__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_and__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__assn_or__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__or__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__and__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__eq__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__neq__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__strict_eq__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__strict_neq__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__lt__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__le__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__gt__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__ge__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__add_ii__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__add_ll__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__add_ff__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__add_dd__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__add_ss__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__sub_ii__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__sub_ll__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__sub_ff__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__sub_dd__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__mul__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__div__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__mod__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__mat__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__pow__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__idx__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);

void __builtin__not__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__neg__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
void __builtin__inv__(
    data_idx_t self, data_idx_t *args, arr_size_t wCnt, arr_size_t nCnt, Frame &frame,
    Context &ctx);
