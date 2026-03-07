/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"

#include <cstdlib>
#include <filesystem>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;

slot_t __sys_get_env__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *key     = norm.get<String *>(0);
    const char *val = std::getenv(key->c_str());
    if (!val)
        return toSlot(String::from("", mm::autoSpace()));
    return toSlot(String::from(val, mm::autoSpace()));
}

slot_t __sys_get_cwd__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string cwd = std::filesystem::current_path().string();
    return toSlot(String::from(cwd, mm::autoSpace()));
}

std::unordered_map<std::string, operator_t> getSysOpsMap() {
    return {{"get_env", __sys_get_env__}, {"get_cwd", __sys_get_cwd__}};
}
