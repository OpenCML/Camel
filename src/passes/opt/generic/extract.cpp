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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


#include "camel/passes/opt/generic.h"
#include "camel/runtime/writings.h"

#include <format>
#include <stdexcept>

const camel::runtime::Program *
ExtractPass::apply(const camel::runtime::Program *program, std::ostream &) {
    const auto model = camel::runtime::findCostModel(cost_);
    if (!model) {
        throw std::runtime_error(std::format("There is no cost model '{}'.", cost_));
    }
    return camel::runtime::extractWritings(context_, program, *model);
}
