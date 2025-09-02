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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "common/operator.h"
#include "common/type.h"

data_ptr_t __builtin__assn__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_add__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_sub__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_mul__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_div__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_mod__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_mat__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_exp__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_and__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__assn_or__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);

data_ptr_t __builtin__or__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__and__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__eq__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__neq__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__strict_eq__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__strict_neq__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__lt__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__le__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__gt__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__ge__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);

data_ptr_t __builtin__add__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__sub__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__mul__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__div__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__mod__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__mat__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__exp__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
data_ptr_t __builtin__idx__(Context &ctx, const data_vec_t &with, const data_vec_t &norm);
