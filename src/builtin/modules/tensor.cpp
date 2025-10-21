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
 * Created: Oct. 21, 2025
 * Updated: Oct. 21, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "builtin/executors/builtin.h"
#include "core/context/context.h"

TensorBuiltinModule::TensorBuiltinModule(context_ptr_t ctx) : BuiltinModule("tensor", ctx) {
    exportBuiltinOperator(
        "eye",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/eye");
    exportBuiltinOperator(
        "zeros",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/zeros");
    exportBuiltinOperator(
        "ones",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/ones");
    exportBuiltinOperator(
        "diag",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/diag");
    exportBuiltinOperator(
        "linspace",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/linspace");
    exportBuiltinOperator(
        "arange",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/arange");

    exportBuiltinOperator(
        "add",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Any(), false}},
        Type::Any(),
        ":tensor/add");
    exportBuiltinOperator(
        "subtract",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Any(), false}},
        Type::Any(),
        ":tensor/subtract");
    exportBuiltinOperator(
        "multiply",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Any(), false}},
        Type::Any(),
        ":tensor/multiply");
    exportBuiltinOperator(
        "divide",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Any(), false}},
        Type::Any(),
        ":tensor/divide");

    exportBuiltinOperator(
        "reshape",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Any(), false}},
        Type::Any(),
        ":tensor/reshape");
    exportBuiltinOperator(
        "transpose",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/transpose");
    exportBuiltinOperator(
        "flatten",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/flatten");

    exportBuiltinOperator(
        "concat",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Any(), false}, {Type::Int(), false}},
        Type::Any(),
        ":tensor/concat");
    exportBuiltinOperator(
        "stack",
        param_init_list_t{},
        {{Type::Any(), false}, {Type::Int(), false}},
        Type::Any(),
        ":tensor/stack");

    exportBuiltinOperator(
        "sum",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/sum");
    exportBuiltinOperator(
        "mean",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/mean");
    exportBuiltinOperator(
        "min",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/min");
    exportBuiltinOperator(
        "max",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/max");

    exportBuiltinOperator(
        "norm_l1",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/norm_l1");
    exportBuiltinOperator(
        "norm_l2",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/norm_l2");

    exportBuiltinOperator(
        "sin",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/sin");
    exportBuiltinOperator(
        "cos",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/cos");
    exportBuiltinOperator(
        "exp",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Any(),
        ":tensor/exp");
}

bool TensorBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("tensor", [&]() {
        return BasicBuiltinExecutor::create(context_);
    });
    loaded_ = true;
    return true;
}