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
 * Created: Sep. 22, 2025
 * Updated: Sep. 22, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "this.h"

ThisBuiltinModule::ThisBuiltinModule(context_ptr_t ctx) : BuiltinModule("this", ctx) {
    exportBuiltinOperator(
        "zen",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":zen");
}

bool ThisBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}