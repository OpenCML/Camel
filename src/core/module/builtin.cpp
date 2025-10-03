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
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin.h"

#include "builtin/modules/fs.h"
#include "builtin/modules/globals.h"
#include "builtin/modules/io.h"
#include "builtin/modules/json.h"
#include "builtin/modules/math.h"
#include "builtin/modules/os.h"
#include "builtin/modules/profiler.h"
#include "builtin/modules/random.h"
#include "builtin/modules/re.h"
#include "builtin/modules/sys.h"
#include "builtin/modules/this.h"
#include "builtin/modules/time.h"

std::unordered_map<std::string, std::function<std::shared_ptr<Module>(context_ptr_t ctx)>>
    builtinModuleFactories = {
        {"", [](context_ptr_t ctx) { return GlobalsBuiltinModule::create(ctx); }},
        {"io", [](context_ptr_t ctx) { return IOBuiltinModule::create(ctx); }},
        {"fs", [](context_ptr_t ctx) { return FileBuiltinModule::create(ctx); }},
        {"os", [](context_ptr_t ctx) { return OSBuiltinModule::create(ctx); }},
        {"re", [](context_ptr_t ctx) { return REBuiltinModule::create(ctx); }},
        {"sys", [](context_ptr_t ctx) { return SysBuiltinModule::create(ctx); }},
        {"math", [](context_ptr_t ctx) { return MathBuiltinModule::create(ctx); }},
        {"time", [](context_ptr_t ctx) { return TimeBuiltinModule::create(ctx); }},
        {"json", [](context_ptr_t ctx) { return JsonBuiltinModule::create(ctx); }},
        {"random", [](context_ptr_t ctx) { return RandomBuiltinModule::create(ctx); }},
        {"this", [](context_ptr_t ctx) { return ThisBuiltinModule::create(ctx); }},
        {"profiler", [](context_ptr_t ctx) { return ProfilerBuiltinModule::create(ctx); }},
};
