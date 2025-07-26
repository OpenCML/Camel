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
 * Created: Aug. 17, 2024
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "init.h"
#include "common/type.h"

inline operator_ptr_t makeOperator(const std::string &name, const func_type_ptr_t &&type, OperatorFunction &&func) {
    return std::make_shared<Operator>(name, std::move(type), std::move(func));
}

inline func_type_ptr_t makeFuncType(const param_init_list &with, const param_init_list &norm,
                                    const type_ptr_t &returnType) {
    return std::make_shared<FunctionType>(with, norm, returnType);
}

void initGlobalOperators() {
    if (globalOperatorsInitialized) {
        return;
    }
    globalOperatorsInitialized = true;

    registerOperator(makeOperator(
        "__assn__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_add__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_sub__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_mul__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_div__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_mod__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_mat__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_exp__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_and__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__assn_or__",
        makeFuncType(param_init_list{},
                     param_init_list{{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__or__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__and__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__eq__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__neq__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__strict_eq__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__strict_neq__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__lt__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__le__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__gt__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__ge__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__add__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__sub__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__mul__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__div__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__mod__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__mat__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__exp__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(makeOperator(
        "__idx__",
        makeFuncType(param_init_list{},
                     param_init_list{{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
                     anyTypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));
}
