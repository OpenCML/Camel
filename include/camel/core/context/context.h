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
 * Updated: May. 01, 2026
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
#include <span>
#include <unordered_map>
#include <vector>

namespace camel::runtime {
class GCGraph;
class GCGraphManager;
} // namespace camel::runtime

namespace camel::core::module {
class Module;
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

    std::string toString() const;
};

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    error::DiagsConfig diagConfig_;

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;
    std::unordered_map<std::string, module_ptr_t> builtinModules_;

    std::unique_ptr<ExecutorManager> exeMgr_;
    std::unique_ptr<camel::runtime::GCGraphManager> runtimeGraphMgr_;
    error::diagnostic_sink_ptr_t runtimeDiagSink_;
    error::runtime_error_reporter_ptr_t runtimeErrorReporter_;
    camel::source::source_context_ptr_t sourceContext_;
    std::unordered_map<std::string, void *>
        loadedDllHandles_; // .cmo path -> handle; keep DLLs loaded.
    std::optional<int> processExitCode_;
    std::function<void(camel::runtime::GCGraph *, slot_t)> entryResultHandler_;
    /** Most recent reason a ".cmo found but failed to load" case occurred, used by ModuleNotFound
     * diagnostics. */
    mutable std::string lastCmoLoadError_;
    /// See setPassFact.
    std::unordered_map<std::string, std::shared_ptr<const void>> passFacts_;

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
    const ExecutorManager &execMgr() const { return *exeMgr_; }

    void setMainModule(module_ptr_t module) { mainModule_ = module; }

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

    /** Called by loadCmoModule to retain a loaded .cmo handle and prevent unloading. */
    void addLoadedDll(const std::string &path, void *handle) { loadedDllHandles_[path] = handle; }

    module_ptr_t
    importModule(const std::string &rawModuleName, const std::string &currentModuleName = "");

  private:
    void registerRuntimeGraphDebugInfo(camel::runtime::GCGraph *runtimeRoot);
};

} // namespace camel::core::context
