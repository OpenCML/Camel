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
 * Updated: Sep. 27, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "os.h"

OSBuiltinModule::OSBuiltinModule(context_ptr_t ctx) : BuiltinModule("os", ctx) {
    exportBuiltinOperator(
        "whoami",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":os/whoami");
    exportBuiltinOperator(
        "set_terminal_raw_mode",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":os/set_terminal_raw_mode");
    exportBuiltinOperator(
        "has_input",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":os/has_input");
    exportBuiltinOperator(
        "get_char",
        param_init_list{},
        {{"value", Type::Any(), nullptr, false}},
        Type::Void(),
        ":os/get_char");
}

bool OSBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
