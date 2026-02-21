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
 * Updated: Feb. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

slot_t __builtin__assn__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__assn_add_i__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_add_l__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_add_f__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_add_d__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__assn_sub_i__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_sub_l__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_sub_f__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_sub_d__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__assn_mul_i__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_mul_l__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_mul_f__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_mul_d__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__assn_div_i__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_div_l__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_div_f__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_div_d__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__assn_mod__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_mat__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_pow__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_and__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__assn_or__(ArgsView &with, ArgsView &norm, Context &ctx);

template <typename T> slot_t __builtin__lt__(ArgsView &with, ArgsView &norm, Context &ctx) {
    T l = norm.get<T>(0);
    T r = norm.get<T>(1);
    return toSlot(Bool(l < r));
}

template <typename T> slot_t __builtin__le__(ArgsView &with, ArgsView &norm, Context &ctx) {
    T l = norm.get<T>(0);
    T r = norm.get<T>(1);
    return toSlot(Bool(l <= r));
}

template <typename T> slot_t __builtin__gt__(ArgsView &with, ArgsView &norm, Context &ctx) {
    T l = norm.get<T>(0);
    T r = norm.get<T>(1);
    return toSlot(Bool(l > r));
}

template <typename T> slot_t __builtin__ge__(ArgsView &with, ArgsView &norm, Context &ctx) {
    T l = norm.get<T>(0);
    T r = norm.get<T>(1);
    return toSlot(Bool(l >= r));
}

slot_t __builtin__eq__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__ne__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__strict_eq__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__strict_ne__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__and__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__or__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__add_ii__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__add_ll__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__add_ff__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__add_dd__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__add_ss__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__sub_ii__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__sub_ll__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__sub_ff__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__sub_dd__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__mul__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__div__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__mod__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__mat__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__pow__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__idx__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __builtin__not__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__neg__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __builtin__inv__(ArgsView &with, ArgsView &norm, Context &ctx);
