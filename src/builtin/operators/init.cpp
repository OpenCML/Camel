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

void initGlobalOperators() {
    if (globalOperatorsInitialized) {
        return;
    }
    globalOperatorsInitialized = true;

    registerOperator(std::make_shared<Operator>(
        "__assn__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_add__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_sub__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_mul__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_div__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_mod__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_mat__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_exp__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_and__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__assn_or__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__or__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__and__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__eq__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__neq__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__strict_eq__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__strict_neq__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__lt__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__le__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__gt__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__ge__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__add__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__sub__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__mul__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__div__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__mod__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__mat__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__exp__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        "__idx__",
        std::make_shared<FunctionType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));
}
