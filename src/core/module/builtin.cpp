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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/module/builtin.h"
#include "camel/core/data/composite/func.h"
#include "camel/core/operator.h"

#include "builtin/modules/globals.h"
#include "builtin/modules/profiler.h"

using namespace camel::core::context;

namespace camel::core::module {

std::unordered_map<std::string, std::function<std::shared_ptr<Module>(context_ptr_t ctx)>>
    builtinModuleFactories = {
        {"", [](context_ptr_t ctx) { return GlobalsBuiltinModule::create(ctx); }},

#ifndef NDEBUG
        {"profiler", [](context_ptr_t ctx) { return ProfilerBuiltinModule::create(ctx); }},
#endif
};

} // namespace camel::core::module
