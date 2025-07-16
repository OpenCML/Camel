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

void initOperators() {
    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__copy__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__cast__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__type__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__index__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__as__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__is__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__add__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__sub__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__mul__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__div__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__mod__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__pow__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__inter__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__union__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_add__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_sub__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_mul__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_div__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_mod__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_pow__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_inter__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__assn_union__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__lt__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__gt__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__le__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__ge__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__eq__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__ne__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__and__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__or__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__not__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__neg__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctionType>(
            "__rev__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));
}
