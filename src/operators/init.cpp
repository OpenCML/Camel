/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 19, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "init.h"
#include "common/type.h"

void initOperators() {
    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__copy__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__cast__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__type__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__index__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__as__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__is__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__add__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__sub__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__mul__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__div__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__mod__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__pow__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__inter__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__union__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_add__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_sub__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_mul__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_div__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_mod__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_pow__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_inter__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__assn_union__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__lt__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__gt__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__le__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__ge__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__eq__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__ne__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__and__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__or__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__not__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__neg__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));

    registerOperator(std::make_shared<Operator>(
        std::make_shared<FunctorType>(
            "__rev__",
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));
}
