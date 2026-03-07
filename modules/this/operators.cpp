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

#include <iostream>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;

slot_t __this_zen__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string zen =
#include "ZEN"
        ;
    String *str = String::from(zen, mm::autoSpace());
    return toSlot(str);
}

std::unordered_map<std::string, operator_t> getThisOpsMap() { return {{"zen", __this_zen__}}; }
