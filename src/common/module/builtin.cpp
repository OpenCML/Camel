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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin.h"

#include "builtin/modules/basic.h"
#include "builtin/modules/fs.h"
#include "builtin/modules/io.h"
#include "builtin/modules/json.h"
#include "builtin/modules/math.h"
#include "builtin/modules/os.h"
#include "builtin/modules/random.h"
#include "builtin/modules/re.h"
#include "builtin/modules/sys.h"
#include "builtin/modules/time.h"

data_ptr_t __not_implemented__(Context &ctx, const data_vec_t &with, const data_vec_t &norm) {
    ASSERT(false, "Not implemented");
    return nullptr;
}

std::unordered_map<std::string, std::function<std::shared_ptr<Module>()>> builtinModuleFactories = {
    {"", [] { return BasicBuiltinModule::create(); }},
    {"io", [] { return IOBuiltinModule::create(); }},
    {"fs", [] { return FileBuiltinModule::create(); }},
    {"os", [] { return OSBuiltinModule::create(); }},
    {"re", [] { return REBuiltinModule::create(); }},
    {"sys", [] { return SysBuiltinModule::create(); }},
    {"math", [] { return MathBuiltinModule::create(); }},
    {"time", [] { return TimeBuiltinModule::create(); }},
    {"json", [] { return JsonBuiltinModule::create(); }},
    {"random", [] { return RandomBuiltinModule::create(); }},
};
std::unordered_map<std::string, module_ptr_t> builtinModules;

std::optional<module_ptr_t> getBuiltinModule(const std::string &name) {
    auto it = builtinModules.find(name);
    if (it != builtinModules.end()) {
        return it->second;
    }

    auto factoryIt = builtinModuleFactories.find(name);
    if (factoryIt != builtinModuleFactories.end()) {
        module_ptr_t module = factoryIt->second();
        builtinModules[name] = module;
        return module;
    }

    return std::nullopt;
}