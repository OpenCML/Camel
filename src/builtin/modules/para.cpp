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
 * Created: Oct. 09, 2025
 * Updated: Oct. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "para.h"

ParaBuiltinModule::ParaBuiltinModule(context_ptr_t ctx) : BuiltinModule("para", ctx) {
    exportBuiltinOperator(
        "unordered_reduce",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Void(),
        ":mark/unordered_reduce");
    exportBuiltinOperator(
        "unordered_foreach",
        param_init_list_t{},
        {{Type::Any(), false}},
        Type::Void(),
        ":mark/unordered_foreach");
}

bool ParaBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
