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

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/module/userdef.h"

#include <iosfwd>
#include <vector>

namespace camel_cli {

/// Which modules the command is asking about. "Did anything fail?" has no useful answer until
/// the modules are named: the modules the program is made of and every module the context happens
/// to hold are different sets, and asking the wrong one either hides a program's failure or
/// invents one. The scope is required at every call site so that the choice is written there.
enum class ModuleScope {
    /// The entry module and every module it imports, directly or transitively: the program a
    /// run, a check or a `--timeit` iteration is about. A pass library the pass list loaded is
    /// not part of it, even though it lives in the same context; a pass that cannot be resolved
    /// is reported by pass resolution, with the library's first error as a secondary location.
    ProgramDependencies,
    /// Every user module in the context. A command that lists or dumps modules of the whole
    /// context asks this: `inspect --ops` is the inventory every imported module fills, so a
    /// module that failed to compile makes the listing incomplete wherever it sits (H172).
    AllUserModules,
};

/// What the modules a command just compiled or loaded say about the one question every command
/// that renders something out of them has to ask: did anything fail?
///
/// The answer is read from the modules' diagnostics, not from
/// `UserDefinedModule::loaded()`: a command may stop before the whole program is built
/// (`inspect --gct` lowers the construction tree and nothing after it), and every failure the
/// front end has is stored in a module's diagnostics. The declaration-module list is what turns
/// "a diagnostic somewhere" into "this declaration file did not load": the operator inventory
/// is what such a file fills, and a shortened inventory that exits 0 is H166/H172's silence.
struct ModuleFailures {
    /// At least one user module *in the requested scope* carries an error.
    bool any = false;
    /// The modules that carry an error *in the requested scope*, in the order
    /// `Context::allUserModules()` returns them. This is what `reportModuleFailures` prints: a
    /// module outside the scope is a pass library or another module the command loaded but did
    /// not ask about, and its diagnostics would otherwise answer a question nobody asked (H210).
    std::vector<camel::core::module::module_ptr_t> modules;
    /// The declaration modules (`math.cml` beside `math.cmo`) that carry an error, in the order
    /// `Context::allUserModules()` returns them.
    std::vector<const camel::core::module::UserDefinedModule *> declarationModules;
};

/// The one place that answers "did anything fail?" for a command that compiled or loaded
/// modules. It answers about `scope` only: `ProgramDependencies` is the entry module's import
/// closure, `AllUserModules` is every user module in the context. A new output path asks this
/// before printing, instead of copying a `!loaded()` test that does not fit every compile depth.
ModuleFailures findModuleFailures(
    const camel::core::context::context_ptr_t &ctx, ModuleScope scope);

/// Dump the diagnostics of the failed modules in `failures` (`Context::dumpModuleDiagnostics`),
/// never of every module that happens to carry an error: `findModuleFailures` already decided
/// which ones this command asked about. When `declarationFilesOut` is not null it also receives
/// one line per failed declaration module, naming the module and its file (H172's `--ops`
/// contract: the channel that carries the listing stays clean).
void reportModuleFailures(
    const camel::core::context::context_ptr_t &ctx,
    const ModuleFailures &failures,
    std::ostream &diagnosticsOut,
    bool json,
    std::ostream *declarationFilesOut = nullptr,
    bool wrapInArray                 = true);

/// `findModuleFailures` + `reportModuleFailures` + the answer in one call: true when something
/// in `scope` failed and was reported, so the caller returns 1 and prints nothing it built; false
/// when the caller may print. A command that renders output out of the modules calls this and
/// cannot forget the question by not looking, or forget the scope by not naming it.
bool failIfModuleErrors(
    const camel::core::context::context_ptr_t &ctx,
    ModuleScope scope,
    std::ostream &diagnosticsOut,
    bool json,
    std::ostream *declarationFilesOut = nullptr,
    bool wrapInArray                 = true);

} // namespace camel_cli