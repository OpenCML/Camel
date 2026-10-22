/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and
 * conditions of the MIT license. You may obtain a copy of
 * the MIT license at: [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT
 * WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module_failures.h"

#include <memory>
#include <ostream>

namespace camel_cli {

using camel::core::module::UserDefinedModule;

ModuleFailures findModuleFailures(const camel::core::context::context_ptr_t &ctx) {
    ModuleFailures failures;
    for (const auto &module : ctx->allUserModules()) {
        auto userModule = std::dynamic_pointer_cast<UserDefinedModule>(module);
        if (!userModule || !userModule->diagnostics() ||
            !userModule->diagnostics()->hasErrors()) {
            continue;
        }
        failures.any = true;
        if (userModule->declarationModule()) {
            failures.declarationModules.push_back(userModule.get());
        }
    }
    return failures;
}

void reportModuleFailures(
    const camel::core::context::context_ptr_t &ctx,
    const ModuleFailures &failures,
    std::ostream &diagnosticsOut,
    bool json,
    std::ostream *declarationFilesOut,
    bool wrapInArray) {
    if (declarationFilesOut != nullptr) {
        for (const UserDefinedModule *module : failures.declarationModules) {
            *declarationFilesOut << "camel: the declaration module '" << module->name()
                                 << "' failed to load from '" << module->path() << "'"
                                 << std::endl;
        }
    }
    ctx->dumpAllModuleDiagnostics(diagnosticsOut, json, wrapInArray);
}

bool failIfModuleErrors(
    const camel::core::context::context_ptr_t &ctx,
    std::ostream &diagnosticsOut,
    bool json,
    std::ostream *declarationFilesOut,
    bool wrapInArray) {
    const ModuleFailures failures = findModuleFailures(ctx);
    if (!failures.any) {
        return false;
    }
    reportModuleFailures(
        ctx, failures, diagnosticsOut, json, declarationFilesOut, wrapInArray);
    return true;
}

} // namespace camel_cli