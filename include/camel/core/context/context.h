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
 * Created: Aug. 18, 2024
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/slot.h"
#include "camel/core/source/manager.h"

#include <fstream>
#include <functional>
#include <memory>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camel::runtime {
class GCGraph;
class GCGraphManager;
class DefinitionStore;
} // namespace camel::runtime

namespace camel::runtime::fact {
class FactStore;
} // namespace camel::runtime::fact

namespace camel::core::module {
class Module;
}

namespace camel::compile {
class SemanticRecorder;
}

// Forward declarations for execute (avoid circular include with executor.h)
class Executor;
class ExecutorManager;

namespace camel::core::context {

class Frame;
class Context;

using context_ptr_t = std::shared_ptr<Context>;
using module_ptr_t  = std::shared_ptr<module::Module>;

struct EntryConfig {
    std::string entryDir;                 // root path of the context, used for loading modules
    std::string entryFile;                // entry module path
    std::vector<std::string> searchPaths; // search paths for modules
    // Upper bound on worker threads of parallel schedulers (`-t` on the command line); 0 means
    // not given: the scheduler uses the number of hardware threads.
    size_t maxThreads = 0;
    // Limit on non-tail recursion (`--max-recursion-depth`) and on nested calls of the macro
    // evaluator (`--max-macro-depth`); 0 means not given: see camel/execute/limits.h for the
    // environment variables and the defaults.
    size_t maxRecursionDepth = 0;
    size_t maxMacroDepth     = 0;

    // Editor analysis can omit the executable entry and supply unsaved module texts.
    bool requireEntry = true;
    std::function<std::optional<std::string>(const std::string &)> sourceOverride;

    std::string toString() const;
};

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    error::DiagsConfig diagConfig_;

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;
    /// The type declarations being built right now, outermost first, across every module (see
    /// enterTypeDeclaration).
    std::vector<std::pair<const module::Module *, std::string>> typeDeclarations_;
    /// The type declarations found on a cycle (see markTypeCycle).
    std::set<std::pair<const module::Module *, std::string>> typeCycleMembers_;
    std::unordered_map<std::string, module_ptr_t> builtinModules_;

    std::unique_ptr<ExecutorManager> exeMgr_;
    std::unique_ptr<camel::runtime::GCGraphManager> runtimeGraphMgr_;
    /// See facts(). Created on first use.
    std::unique_ptr<camel::runtime::fact::FactStore> facts_;
    /// See definitions(). Created on first use.
    std::unique_ptr<camel::runtime::DefinitionStore> definitions_;
    error::diagnostic_sink_ptr_t runtimeDiagSink_;
    error::runtime_error_reporter_ptr_t runtimeErrorReporter_;
    camel::source::source_context_ptr_t sourceContext_;
    std::unordered_map<std::string, void *>
        loadedDllHandles_; // .cmo path -> handle; keep DLLs loaded.
    std::optional<int> processExitCode_;
    std::function<void(camel::runtime::GCGraph *, slot_t)> entryResultHandler_;
    /// See setEntryResultDeclared.
    bool entryResultDeclared_ = true;
    /** Most recent reason a ".cmo found but failed to load" case occurred, used by ModuleNotFound
     * diagnostics. */
    mutable std::string lastCmoLoadError_;
    /// See setPassFact.
    std::unordered_map<std::string, std::shared_ptr<const void>> passFacts_;
    /// See setSemanticRecorder.
    camel::compile::SemanticRecorder *semanticRecorder_ = nullptr;
    /// See graphBuildSession.
    std::shared_ptr<void> graphBuildSession_;
    /// The regular files of each directory a module was looked up in (getModulePathAndKind),
    /// by their names without the extension, and the names that have a `.cmo`.
    struct DirectoryListing {
        std::unordered_map<std::string, std::string> byStem;
        std::unordered_set<std::string> cmo;
    };
    std::unordered_map<std::string, DirectoryListing> directoryListings_;

    std::optional<module_ptr_t> getBuiltinModule(const std::string &name);
    std::vector<std::string>
    getModuleNameCandidates(const std::string &currentModule, const std::string &rawImportName);
    std::string
    resolveRelativeModuleName(const std::string &currentModule, const std::string &importName);
    /** Look up the file for a module. Any suffix other than .cmo is treated as source code; .cmo is
     * treated as a dynamic library. An empty first value means not found. */
    std::pair<std::string, bool> getModulePathAndKind(const std::string &moduleName);
    std::string getModulePath(const std::string &moduleName);
    bool moduleFileExists(const std::string &moduleName);
    module_ptr_t tryLoadModule(const std::string &moduleName);
    /** Collect lookup attempts for a missing module so error messages can report them. */
    std::vector<std::string> getModuleSearchDiagnostics(const std::string &moduleName) const;
    /** Return the base search paths that were actually consulted; for .cmo load failures we only

     * * list paths. */
    std::vector<std::string> getSearchPathBases() const;

    Context(const EntryConfig &entryConf, const error::DiagsConfig &diagConf);

  public:
    ~Context();

    static context_ptr_t create(
        const EntryConfig &entryConf       = EntryConfig(),
        const error::DiagsConfig &diagConf = error::DiagsConfig());

    bool requireEntry() const { return entryConfig_.requireEntry; }
    std::optional<std::string> sourceOverride(const std::string &path) const {
        return entryConfig_.sourceOverride ? entryConfig_.sourceOverride(path) : std::nullopt;
    }
    const std::string &entryDir() const { return entryConfig_.entryDir; }
    size_t maxThreads() const { return entryConfig_.maxThreads; }
    size_t maxRecursionDepth() const { return entryConfig_.maxRecursionDepth; }
    size_t maxMacroDepth() const { return entryConfig_.maxMacroDepth; }
    error::DiagsConfig diagConfig() const { return diagConfig_; }
    module_ptr_t mainModule() const { return mainModule_; }
    error::diagnostics_ptr_t rtmDiags() const { return runtimeDiagSink_; }
    error::diagnostic_sink_ptr_t runtimeDiagSink() const { return runtimeDiagSink_; }
    error::runtime_error_reporter_ptr_t runtimeErrorReporter() const {
        return runtimeErrorReporter_;
    }
    camel::source::source_context_ptr_t sourceContext() const { return sourceContext_; }
    camel::runtime::GCGraph *runtimeRootGraph();
    camel::runtime::GCGraph *materializeRuntimeRoot();
    camel::runtime::GCGraph *adoptRuntimeRoot(camel::runtime::GCGraph *runtimeRoot);
    camel::runtime::GCGraph *installRuntimeRoot(camel::runtime::GCGraph *runtimeRoot);
    camel::runtime::GCGraph *currentRuntimeRoot() const;
    void clearRuntimeGraphs();
    /// See GCGraphManager::trackDetached / releaseDetached.
    void trackDetachedRuntimeGraphs(std::span<camel::runtime::GCGraph *const> graphs);
    void releaseDetachedRuntimeGraphs();
    /// See GCGraphManager::pin: graphs the core keeps for the context's lifetime (the
    /// definition graphs of operators).
    void pinRuntimeGraphs(std::span<camel::runtime::GCGraph *const> graphs);
    /// The program's graph facts (camel/runtime/facts.h): what passes and back ends derived
    /// about its graphs, each with a stamp that a read checks. Lives as long as the context;
    /// a fact survives rewrites of the program and reads as absent once it no longer holds.
    camel::runtime::fact::FactStore &facts();
    /// The definition graphs of operators built in this context (camel/runtime/definitions.h):
    /// their libraries are compiled the first time a definition is asked for, and their
    /// graphs kept (pinned) for as long as the context.
    camel::runtime::DefinitionStore &definitions();
    const ExecutorManager &execMgr() const { return *exeMgr_; }

    void setMainModule(module_ptr_t module) { mainModule_ = module; }

    // ---- Type declarations in progress (design principles 2.13) ----------------------------
    // Modules may import each other's types (2.11). A type declaration of one module that
    // needs a type of another module compiles that module's types in the middle of its own
    // declaration; when that module needs, in turn, the type being declared, the types form a
    // cycle across the modules. Each module has its own construction-tree builder, so the
    // declarations in progress are kept here, where all of them see them.
    /// Records that the type `name` of `mod` is being declared (until the matching leave).
    void enterTypeDeclaration(const module::Module *mod, const std::string &name) {
        typeDeclarations_.emplace_back(mod, name);
    }
    void leaveTypeDeclaration() { typeDeclarations_.pop_back(); }
    /// The declarations in progress, outermost first.
    const std::vector<std::pair<const module::Module *, std::string>> &
    typeDeclarationsInProgress() const {
        return typeDeclarations_;
    }
    /// Remembers the members of a cycle that was reported, so that the failures that follow
    /// from it (in the other modules of the cycle) are not reported again.
    void markTypeCycle(const module::Module *mod, const std::string &name) {
        typeCycleMembers_.emplace(mod, name);
    }
    bool onReportedTypeCycle(const module::Module *mod, const std::string &name) const {
        return typeCycleMembers_.contains({mod, name});
    }

    /// Returns main module plus all UserDefinedModules from modules_ (excluding builtin)
    std::vector<module_ptr_t> allUserModules() const;

    /// The built-in module and every module imported so far, sorted by name.
    std::vector<module_ptr_t> importedModules() const;

    /// Dump diagnostics from main and all imported modules that have errors; json=true for JSON
    /// format
    void dumpAllModuleDiagnostics(std::ostream &os, bool json) const;

    /// Dump the Warn-level diagnostics of the user modules that compiled without an error (the
    /// modules with errors show theirs through dumpAllModuleDiagnostics). Text: one line per
    /// warning; JSON: one array, the same objects as the error array. Nothing is written when
    /// there is no warning. Returns the number of warnings written. `extra` are warnings that
    /// belong to no module (about the command line), written after the modules' in the same
    /// form, so that JSON output stays one array.
    size_t dumpModuleWarnings(
        std::ostream &os, bool json,
        const std::vector<camel::core::error::Diagnostic> &extra = {}) const;

    void registerExecutorFactory(std::string name, std::function<std::shared_ptr<Executor>()> fact);

    void clearProcessExitCode() { processExitCode_.reset(); }
    /** The program ended itself with `exit(code)` (camel/core/process_exit.h). */
    void setProcessExitCode(int code) { processExitCode_ = code; }
    /** Every scheduling pass reports the value the entry graph returned here. */
    void captureProcessExitCode(camel::runtime::GCGraph *graph, slot_t result);
    /**
     * Who else wants the value the entry graph returned (camel/execute/entry_args.h writes it
     * to a file on request). Called from captureProcessExitCode, while the value is live.
     */
    using EntryResultHandler = std::function<void(camel::runtime::GCGraph *, slot_t)>;
    void setEntryResultHandler(EntryResultHandler handler) {
        entryResultHandler_ = std::move(handler);
    }
    /**
     * Whether the entry function declares its return type. Only then is the value it returns
     * the result of the program: the exit code, and what `-o` writes (design principles 2.10).
     * A `main` without a declared return type still has a value (that of its body), which is
     * nobody's result. The front end that knows the declaration says so here; without a word
     * from it the value counts, as it did before the rule.
     */
    void setEntryResultDeclared(bool declared) { entryResultDeclared_ = declared; }
    bool entryResultDeclared() const { return entryResultDeclared_; }
    std::optional<int> processExitCode() const { return processExitCode_; }
    int processExitCodeOr(int fallback = 0) const { return processExitCode_.value_or(fallback); }

    /**
     * A fact a pass leaves for the passes that run after it on this context: something it
     * decided that the graph it hands on does not show (why a call is still a call). The key
     * names the fact and fixes the type of the value; the header that declares the key declares
     * the type and the typed accessors (camel/passes/opt/config.h: the growth budget).
     * A later pass reads a fact to explain what it finds, never to decide what a program means.
     */
    void setPassFact(const std::string &key, std::shared_ptr<const void> value) {
        passFacts_[key] = std::move(value);
    }
    std::shared_ptr<const void> passFact(const std::string &key) const {
        const auto it = passFacts_.find(key);
        return it == passFacts_.end() ? nullptr : it->second;
    }

    /**
     * Who is told what the builders learn about the names and types of the modules compiled on
     * this context (camel/compile/semantic_recorder.h). Null, the default, for a compilation
     * that only wants the graph. Not owned: it must outlive the compilations.
     */
    void setSemanticRecorder(camel::compile::SemanticRecorder *recorder) {
        semanticRecorder_ = recorder;
    }
    camel::compile::SemanticRecorder *semanticRecorder() const { return semanticRecorder_; }

    /**
     * The graph builder of the program compiled on this context: one for all of its modules,
     * which build their functions and their top-level values into one `__root__` graph
     * (design principles 2.11). Opaque here: it belongs to camel/compile/gir, which creates
     * it on first use (camel::compile::gir::Builder::of).
     */
    std::shared_ptr<void> &graphBuildSession() { return graphBuildSession_; }

    /** Called by loadCmoModule to retain a loaded .cmo handle and prevent unloading. */
    void addLoadedDll(const std::string &path, void *handle) { loadedDllHandles_[path] = handle; }

    module_ptr_t
    importModule(const std::string &rawModuleName, const std::string &currentModuleName = "");

  private:
    void registerRuntimeGraphDebugInfo(camel::runtime::GCGraph *runtimeRoot);
};

} // namespace camel::core::context
