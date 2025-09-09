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

#include "ops.h"

data_ptr_t __builtin__assn__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__assn_add__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_sub__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_mul__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_div__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_mod__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_mat__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_exp__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_and__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__assn_or__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}

data_ptr_t __builtin__or__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__and__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__eq__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__neq__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__strict_eq__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__strict_neq__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    return nullptr;
}
data_ptr_t __builtin__lt__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__le__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__gt__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__ge__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }

data_ptr_t __builtin__add__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__sub__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__mul__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__div__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__mod__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__mat__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__exp__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }
data_ptr_t __builtin__idx__(Context &ctx, data_vec_t &with, data_vec_t &norm) { return nullptr; }

const std::pair<std::string, operator_t> opsOpMap[] = {
    {":op/assn", __builtin__assn__},
    {":op/assn_add", __builtin__assn_add__},
    {":op/assn_sub", __builtin__assn_sub__},
    {":op/assn_mul", __builtin__assn_mul__},
    {":op/assn_div", __builtin__assn_div__},
    {":op/assn_mod", __builtin__assn_mod__},
    {":op/assn_mat", __builtin__assn_mat__},
    {":op/assn_exp", __builtin__assn_exp__},
    {":op/assn_and", __builtin__assn_and__},
    {":op/assn_or", __builtin__assn_or__},
    {":op/or", __builtin__or__},
    {":op/and", __builtin__and__},
    {":op/eq", __builtin__eq__},
    {":op/neq", __builtin__neq__},
    {":op/strict_eq", __builtin__strict_eq__},
    {":op/strict_neq", __builtin__strict_neq__},
    {":op/lt", __builtin__lt__},
    {":op/le", __builtin__le__},
    {":op/gt", __builtin__gt__},
    {":op/ge", __builtin__ge__},
    {":op/add", __builtin__add__},
    {":op/sub", __builtin__sub__},
    {":op/mul", __builtin__mul__},
    {":op/div", __builtin__div__},
    {":op/mod", __builtin__mod__},
    {":op/mat", __builtin__mat__},
    {":op/exp", __builtin__exp__},
    {":op/idx", __builtin__idx__}};