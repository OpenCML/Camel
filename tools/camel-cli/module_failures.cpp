// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module_failures.h"

#include <memory>
#include <ostream>
#include <unordered_set>
#include <vector>

namespace camel_cli {

using camel::core::module::Module;
using camel::core::module::UserDefinedModule;

namespace {

/// The entry module and the modules it imports, transitively: what the program is made of.
///
/// This is deliberately not "every module in the context". Resolving a pass list imports the
/// pass's library, and compiling a pass written in Camel imports what that library imports; those
/// modules join the context without becoming dependencies of the program. Reading the import
/// graph is what tells the two apart, and it is why a pass library that fails to compile no
/// longer suppresses the pass resolution that reports it as `InvalidPassParameter`.
///
/// The import sites are the ones the graph builder recorded while it bound the module's imports,
/// including the declaration module a C++ module stands behind: an import of `math` resolves to
/// its `.cml` declaration module, which is what the closure must reach for a declaration-file
/// failure to count as the program's.
std::unordered_set<const Module *>
programDependencySet(const camel::core::context::context_ptr_t &ctx) {
    std::unordered_set<const Module *> dependencies;
    std::vector<camel::core::module::module_ptr_t> pending;
    if (const auto &entry = ctx->mainModule()) {
        pending.push_back(entry);
    }
    while (!pending.empty()) {
        camel::core::module::module_ptr_t module = std::move(pending.back());
        pending.pop_back();
        if (!module || !dependencies.insert(module.get()).second) {
            continue;
        }
        for (const auto &site : module->importSites()) {
            if (site.mod) {
                pending.push_back(site.mod);
            }
        }
    }
    return dependencies;
}

} // namespace

ModuleFailures findModuleFailures(
    const camel::core::context::context_ptr_t &ctx, ModuleScope scope) {
    ModuleFailures failures;
    std::unordered_set<const Module *> inScope;
    if (scope == ModuleScope::ProgramDependencies) {
        inScope = programDependencySet(ctx);
    }
    for (const auto &module : ctx->allUserModules()) {
        if (scope == ModuleScope::ProgramDependencies && !inScope.count(module.get())) {
            continue;
        }
        auto userModule = std::dynamic_pointer_cast<UserDefinedModule>(module);
        if (!userModule || !userModule->diagnostics() || !userModule->diagnostics()->hasErrors()) {
            continue;
        }
        failures.any = true;
        failures.modules.push_back(module);
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
    // Report what the question was about. The context may hold more failed modules than the
    // scope names (a pass library the pass list loaded); printing them would report a
    // failure the command never asked about and, for a pass library, replace the pass
    // resolution's `InvalidPassParameter` with the raw parse diagnostic.
    ctx->dumpModuleDiagnostics(diagnosticsOut, json, wrapInArray, failures.modules);
}

bool failIfModuleErrors(
    const camel::core::context::context_ptr_t &ctx,
    ModuleScope scope,
    std::ostream &diagnosticsOut,
    bool json,
    std::ostream *declarationFilesOut,
    bool wrapInArray) {
    const ModuleFailures failures = findModuleFailures(ctx, scope);
    if (!failures.any) {
        return false;
    }
    reportModuleFailures(
        ctx, failures, diagnosticsOut, json, declarationFilesOut, wrapInArray);
    return true;
}

} // namespace camel_cli