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
 * Updated: Sep. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "math.h"

MathBuiltinModule::MathBuiltinModule(context_ptr_t ctx) : BuiltinModule("math", ctx) {
    exportBuiltinOperator(
        "abs",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/abs");
    exportBuiltinOperator(
        "exp",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/exp");
    exportBuiltinOperator(
        "round",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/round");
    exportBuiltinOperator(
        "ceil",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/ceil");
    exportBuiltinOperator(
        "floor",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/floor");
    exportBuiltinOperator(
        "bin",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/bin");
    exportBuiltinOperator(
        "oct",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/oct");
    exportBuiltinOperator(
        "hex",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":math/hex");
}

bool MathBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
