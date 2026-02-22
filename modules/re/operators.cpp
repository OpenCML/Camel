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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"

#include <regex>

slot_t __re_test__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *pattern = norm.get<String *>(0);
    String *text    = norm.get<String *>(1);
    try {
        std::regex re(pattern->c_str());
        bool match = std::regex_search(text->c_str(), re);
        return toSlot(match);
    } catch (const std::regex_error &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("<re.test> regex error: ") + e.what());
        return NullSlot;
    }
}

std::unordered_map<std::string, operator_t> getReOpsMap() { return {{"test", __re_test__}}; }
