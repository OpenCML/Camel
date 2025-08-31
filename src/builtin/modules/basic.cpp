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

#include "basic.h"
#include "builtin/operators/basic/io.h"
#include "builtin/operators/basic/ops.h"
#include "builtin/operators/basic/str.h"

#define EXPORT_BINARY_OP(name_str, func_name)                                                      \
    exportEntity(                                                                                  \
        name_str,                                                                                  \
        makeOperator(                                                                              \
            name_str,                                                                              \
            makeFuncType(                                                                          \
                param_init_list{},                                                                 \
                {{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},        \
                anyTypePtr),                                                                       \
            __builtin__##func_name##__))

#define EXPORT_ASSN_OP(name_str, func_name)                                                        \
    exportEntity(                                                                                  \
        name_str,                                                                                  \
        makeOperator(                                                                              \
            name_str,                                                                              \
            makeFuncType(                                                                          \
                param_init_list{},                                                                 \
                {{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},      \
                anyTypePtr),                                                                       \
            __builtin__##func_name##__))

BasicBuiltinModule::BasicBuiltinModule() : BuiltinModule("") {
    EXPORT_ASSN_OP("__assn__", assn);
    EXPORT_ASSN_OP("__assn_add__", assn_add);
    EXPORT_ASSN_OP("__assn_sub__", assn_sub);
    EXPORT_ASSN_OP("__assn_mul__", assn_mul);
    EXPORT_ASSN_OP("__assn_div__", assn_div);
    EXPORT_ASSN_OP("__assn_mod__", assn_mod);
    EXPORT_ASSN_OP("__assn_mat__", assn_mat);
    EXPORT_ASSN_OP("__assn_exp__", assn_exp);
    EXPORT_ASSN_OP("__assn_and__", assn_and);
    EXPORT_ASSN_OP("__assn_or__", assn_or);

    EXPORT_BINARY_OP("__or__", or);
    EXPORT_BINARY_OP("__and__", and);
    EXPORT_BINARY_OP("__eq__", eq);
    EXPORT_BINARY_OP("__neq__", neq);
    EXPORT_BINARY_OP("__strict_eq__", strict_eq);
    EXPORT_BINARY_OP("__strict_neq__", strict_neq);
    EXPORT_BINARY_OP("__lt__", lt);
    EXPORT_BINARY_OP("__le__", le);
    EXPORT_BINARY_OP("__gt__", gt);
    EXPORT_BINARY_OP("__ge__", ge);
    EXPORT_BINARY_OP("__add__", add);
    EXPORT_BINARY_OP("__sub__", sub);
    EXPORT_BINARY_OP("__mul__", mul);
    EXPORT_BINARY_OP("__div__", div);
    EXPORT_BINARY_OP("__mod__", mod);
    EXPORT_BINARY_OP("__mat__", mat);
    EXPORT_BINARY_OP("__exp__", exp);
    EXPORT_BINARY_OP("__idx__", idx);
}
