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
 * Updated: Oct. 18, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "init.h"
#include "common/type.h"

void initOperators() {
    registerOperator(std::make_shared<Operator>(
        "__add__",
        std::make_shared<FunctorType>(
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"a", int64TypePtr, nullptr}}),
            std::make_shared<ParamsType>(
                std::initializer_list<std::tuple<std::string, type_ptr_t, data_ptr_t>>{{"b", int64TypePtr, nullptr}}),
            int64TypePtr),
        [](Context &ctx, data_vec_t &&args) -> data_ptr_t { return nullptr; }));
}
