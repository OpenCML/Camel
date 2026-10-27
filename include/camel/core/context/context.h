// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/context/module_resolver.h"
#include "camel/runtime/graph_id.h"
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
class Program;
class DefinitionStore;
} // namespace camel::runtime

namespace camel::runtime::fact {
class FactStore;
} // namespace camel::runtime::fact

namespace camel::core::module {
class Module;
class ProgramExports;
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

/// The worker threads a run may use when nothing gave a number: the machine's core count, at least
/// 1. Reading it creates nothing.
size_t hardwareThreadCount();

/// How many `Context` objects are alive in this process right now. Read-only: it counts what the
/// constructor and the destructor do and changes nothing else, so a reading that builds a context
/// in a scope can tell whether that context was destroyed when the scope ended.
size_t liveContextCount() noexcept;

struct EntryConfig {
    std::string entryDir;                 // root path of the context, used for loading modules
    std::string entryFile;                // entry module path
    std::vector<std::string> searchPaths; // search paths for modules
    // The thread limit of the run (`-t` on the command line); 0 means not given. It is the one
    // number every pool of the run takes its size from: Context::threadLimit() resolves it, the
    // parallel schedulers use it directly and a module installs it into its own pool when it
    // loads. There is no environment variable for it (design principles 9.3).
    size_t maxThreads = 0;
    // Limit on non-tail recursion (`--max-recursion-depth`) and on nested calls of the macro
    // evaluator (`--max-macro-depth`); 0 means not given: see camel/execute/limits.h for the
    // environment variables and the defaults.
    size_t maxRecursionDepth = 0;
    size_t maxMacroDepth     = 0;
    // Limit on the rounds of one loop under std::macro (`--max-macro-rounds`); 0: not given.
    size_t maxMacroRounds = 0;

    // Editor analysis can omit the executable entry and supply unsaved module texts.
    bool requireEntry = true;
    std::function<std::optional<std::string>(const std::string &)> sourceOverride;

    std::string toString() const;
};

/// A tag use the graph builder resolved: its qualified name, the passes its declaration says
/// read it, and where it is written. The command line uses it to warn about a tag that no pass
/// in the pipeline reads: the tag then has no effect.
struct TagUse {
    std::string name;
    std::vector<std::string> readBy;
    camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
};

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    error::DiagsConfig diagConfig_;

    module_ptr_t mainModule_;
    /// The modules loaded from a file, by the canonical path of that file. A module's import
    /// resolves in its own directory first (design principles 10.2), so two modules in different
    /// directories that import the same name are two different modules: the cache is keyed by
    /// the file a name resolved to, not by the name.
    std::unordered_map<std::string, module_ptr_t> modulesByPath_;
    /// Every module loaded so far, in load order, once each: the iteration order of
    /// `allUserModules` and `importedModules` cannot lose one of two same-named modules.
    std::vector<module_ptr_t> loadedModules_;
    /// The pointers in `loadedModules_`, so the same module is listed once.
    std::unordered_set<const module::Module *> loadedModulePtrs_;
    /// The type declarations being built right now, outermost first, across every module (see
    /// enterTypeDeclaration).
    std::vector<std::pair<const module::Module *, std::string>> typeDeclarations_;
    /// The type declarations found on a cycle (see markTypeCycle).
    std::set<std::pair<const module::Module *, std::string>> typeCycleMembers_;
    /// The tag uses the program was compiled with (see noteTagUse).
    std::vector<TagUse> tagUses_;
    std::unordered_map<std::string, module_ptr_t> builtinModules_;

    std::unique_ptr<ExecutorManager> exeMgr_;
    std::unique_ptr<camel::runtime::GCGraphManager> runtimeGraphMgr_;
    camel::runtime::GraphIdAllocator graphIds_;
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
    /// The exported entities of the program this context is building (see programExports).
    /// Declared before the builder session so that the builder, which refers to it, is
    /// destroyed first.
    std::unique_ptr<module::ProgramExports> programExports_;
    /// See graphBuildSession.
    std::shared_ptr<void> graphBuildSession_;
    /// See genericRuleTemplates.
    std::shared_ptr<void> genericRuleTemplates_;
    /// The lookup of a module name: the one order it is searched in, what counts as a module file,
    /// and the directory listings that answer it (include/camel/core/context/module_resolver.h).
    /// It is built from the entry directory and the module roots of this context, and re-rooting a
    /// context drops its listings.
    ModuleResolver resolver_;

    std::optional<module_ptr_t> getBuiltinModule(const std::string &name);
    /// The module names an import spelling stands for, in the order they are looked up. The
    /// grammar allows a relative prefix before a dotted name (`.`/`..`/`...`; see
    /// docs/language/08_top_level_and_grammar.md): its dots drop that many components from the
    /// importing module's own name, so the names returned here are resolved names, never the
    /// spelling itself (design principles 10.2: an import names a module, not a path). `problem`
    /// is set instead when the prefix drops more components than the name has; no candidate and
    /// no directory lookup exists then.
    std::vector<std::string> getModuleNameCandidates(
        const std::string &currentModule,
        const std::string &rawImportName,
        std::string &problem);
    /// The resolved name a relative spelling stands for (`..lib` in `a.b` is `lib`), or empty and
    /// `problem` set when it drops more components than `currentModule` has.
    std::string resolveRelativeModuleName(
        const std::string &currentModule, const std::string &importName, std::string &problem);
    /// Resolve a module name in the order of design principles 10.2, through the one lookup: the
    /// importing module's own directory, then the module roots (`-I` in the order given,
    /// `CAMEL_PACKAGES`), then the standard library. An empty `importerDir` means the entry
    /// program, whose own directory is `entryDir`.
    std::pair<std::string, bool>
    resolveModulePath(const std::string &moduleName, const std::string &importerDir);
    /// The directory of the importing module: the directory of the file it was loaded from, or
    /// the entry file's directory for the entry program and for a program-level import (a pass
    /// library, a definition).
    std::string importerDirectory(const module::Module *importer) const;
    /// The module already loaded from the file `path`, if any: the import cache is keyed by the
    /// file a name resolved to, so two modules that resolved to different files are two modules
    /// even when they have the same name.
    module_ptr_t moduleAtPath(const std::string &path) const;
    /// Register the module a name resolved to and finish loading it (a `.cmo` as a dynamic
    /// library). Null when there is none. `resolvedPath` is the file the lookup found, which for
    /// a `.cmo` with declarations beside it is not `module->path()`.
    module_ptr_t acceptModule(
        module_ptr_t module, const std::string &resolvedPath, const std::string &importName);
    std::string getModulePath(const std::string &moduleName);
    /// Load the module at a path that was resolved for it (a `.cmo` as a dynamic library, any
    /// other file as source). Kept apart from the lookup so a caller can name the directory the
    /// path was found in.
    module_ptr_t loadModuleFrom(const std::string &moduleName, const std::string &path, bool isCmo);

    Context(const EntryConfig &entryConf, const error::DiagsConfig &diagConf);

  public:
    ~Context();

    static context_ptr_t create(
        const EntryConfig &entryConf       = EntryConfig(),
        const error::DiagsConfig &diagConf = error::DiagsConfig());

    /**
     * A handle to this context that does not keep it alive.
     *
     * The context owns the modules, the executor manager and the builders it made, and those
     * objects refer back to it (a module registers its executors and compiles its programs on the
     * context it belongs to, and a pass compiles on the context whose graph it is rewriting). The
     * reference they hold must not be an owning one: a context whose own members hold it is kept
     * alive by itself, never reaches its destructor, and a context that is not the last one of the
     * process is then never destroyed -- neither the allocator registration it gives back nor the
     * deferred releases it drains ever happen, and a run that builds a context per iteration keeps
     * every one of them. A caller that holds an owning `context_ptr_t` passes `handle()` to what
     * the context will own, and the object that receives it must not outlive the context (design
     * principles 10.5: how the registration of an object root set is held is implementation, not
     * semantics).
     */
    context_ptr_t handle() const;

    bool requireEntry() const { return entryConfig_.requireEntry; }
    /// The command that does not execute the program (an inspection or a translation pass)
    /// lifts the entry requirement after the context is created, before the program is built.
    void setRequireEntry(bool value) { entryConfig_.requireEntry = value; }
    std::optional<std::string> sourceOverride(const std::string &path) const {
        return entryConfig_.sourceOverride ? entryConfig_.sourceOverride(path) : std::nullopt;
    }
    const std::string &entryDir() const { return entryConfig_.entryDir; }
    /// The roots a module is looked for under, in order. A pass declaration is a module
    /// declaration, so the pipeline finds `std.cml`, `onnx.cml`, ... here.
    const std::vector<std::string> &searchPaths() const { return entryConfig_.searchPaths; }
    /// Replace the module roots, and drop what the lookup remembered about the directories of the
    /// old ones: a directory listing answers a question about one root list, so a context that is
    /// re-rooted cannot be answered out of a listing another root list produced. A host that
    /// serves more than one program from one context (the Python binding's `load`, which adds a
    /// directory per call) re-roots it here rather than keeping a context whose roots are the ones
    /// of its first call.
    void setModuleSearchPaths(std::vector<std::string> roots);
    size_t maxThreads() const { return entryConfig_.maxThreads; }
    /// The threads the run may use: `maxThreads()` when `-t` gave one, else the hardware
    /// concurrency. The value of record for every pool of the run.
    size_t threadLimit() const;
    size_t maxRecursionDepth() const { return entryConfig_.maxRecursionDepth; }
    size_t maxMacroDepth() const { return entryConfig_.maxMacroDepth; }
    size_t maxMacroRounds() const { return entryConfig_.maxMacroRounds; }
    error::DiagsConfig diagConfig() const { return diagConfig_; }
    module_ptr_t mainModule() const { return mainModule_; }
    error::diagnostics_ptr_t rtmDiags() const { return runtimeDiagSink_; }
    error::diagnostic_sink_ptr_t runtimeDiagSink() const { return runtimeDiagSink_; }
    error::runtime_error_reporter_ptr_t runtimeErrorReporter() const {
        return runtimeErrorReporter_;
    }
    camel::source::source_context_ptr_t sourceContext() const { return sourceContext_; }
    /// The program of the context (camel/runtime/program.h): the front end encodes the main
    /// module's graphs into it the first time it is asked for, and each commit of a rewrite
    /// installs a new one. The pipeline starts from it.
    const camel::runtime::Program *runtimeProgram();
    const camel::runtime::Program *materializeRuntimeProgram();
    const camel::runtime::Program *adoptRuntimeProgram(const camel::runtime::Program *program);
    const camel::runtime::Program *installRuntimeProgram(const camel::runtime::Program *program);
    /// installRuntimeProgram, but only while `expected` is still the installed program (see
    /// GCGraphManager::replaceRootIfCurrent): answers whether it installed it.
    bool installRuntimeProgramIfCurrent(const camel::runtime::Program *expected,
                                        const camel::runtime::Program *program);
    const camel::runtime::Program *currentRuntimeProgram() const;
    void clearRuntimeGraphs();
    /// A new, empty Program owned by the context, for whoever builds one.
    camel::runtime::Program *reserveProgram();
    /// See GCGraphManager::trackDetached / releaseDetached.
    void trackDetachedRuntimeProgram(const camel::runtime::Program *program);
    void releaseDetachedRuntimeGraphs();
    /// See GCGraphManager::settle / rootForRun / enterRun / release.
    void settleRuntimeGraphs(const camel::runtime::Program *keep = nullptr);
    /// See GCGraphManager::collect.
    void collectRuntimeGraphs();
    void rootProgramForRun(const camel::runtime::Program *program);
    void unrootProgramForRun(const camel::runtime::Program *program);
    void enterRun();
    void leaveRun();
    void releaseRuntimeGraph(camel::runtime::GCGraph *graph);
    /// See GCGraphManager::noteUnbound: a graph a rewrite encoded and has not bound in a
    /// program yet, which the collector frees once the pass is over if nothing binds it.
    void noteUnboundRuntimeGraph(camel::runtime::GCGraph *graph);
    /// The newest program of the context that holds this version of `graph` (see
    /// GCGraphManager::programOf), nullptr when none does.
    const camel::runtime::Program *
    programOfRuntimeGraph(const camel::runtime::GCGraph *graph) const;
    /// See GCGraphManager::pin: programs the core keeps for the context's lifetime (the
    /// definition graphs of operators).
    void pinRuntimeProgram(const camel::runtime::Program *program);
    /// The allocator of the program's GraphIds (camel/runtime/graph_id.h): every graph the
    /// front end builds and every graph a rewrite creates takes its id from here.
    camel::runtime::GraphIdAllocator &graphIds() { return graphIds_; }
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

    // ---- Type declarations in progress (design principles 10.1) ----------------------------
    // Modules may import each other's types (10.1). A type declaration of one module that
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

    // ---- Tags written in the program (design principles 6.2, 9.4) ----------------------------
    void noteTagUse(TagUse use) { tagUses_.push_back(std::move(use)); }
    const std::vector<TagUse> &tagUses() const { return tagUses_; }
    void clearTagUses() { tagUses_.clear(); }

    /// Returns main module plus all UserDefinedModules loaded so far (excluding builtin), once
    /// each even when two of them have the same name.
    std::vector<module_ptr_t> allUserModules() const;

    /// The built-in module and every module imported so far, sorted by name.
    std::vector<module_ptr_t> importedModules() const;

    /// Dump diagnostics from main and all imported modules that have errors; json=true for JSON
    /// format. `wrapInArray` is `Diagnostics::dump`'s: with false (JSON only) the objects are
    /// written without the surrounding `[` and `]`, so a caller that is itself writing a JSON
    /// object can nest them.
    void dumpAllModuleDiagnostics(std::ostream &os, bool json, bool wrapInArray = true) const;

    /// Dump the diagnostics of those of `modules` that have errors, in the order given, with
    /// the rendering and the `[imported by ...]` lines of `dumpAllModuleDiagnostics`. A caller
    /// that already decided which modules its command is about (a program's import closure and
    /// not a pass library, `tools/camel-cli/module_failures.h`) prints those and does not
    /// re-implement the rendering; `dumpAllModuleDiagnostics` is this over every user module.
    void dumpModuleDiagnostics(
        std::ostream &os, bool json, bool wrapInArray,
        const std::vector<module_ptr_t> &modules) const;

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
     * the result of the program: the exit code, and what `-o` writes (design principles 9.8).
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
     * (design principles 10.1). Opaque here: it belongs to camel/compile/gir, which creates
     * it on first use (camel::compile::gir::Builder::of).
     */
    std::shared_ptr<void> &graphBuildSession() { return graphBuildSession_; }

    /**
     * The templates of the generic rules this context has compiled, and the instances made from
     * them: one table per context, indexed by the id a rule value keeps (`ruleId - 1`), so that a
     * rule value holds an id rather than a pointer and an instance is made once (the rule of
     * 2026-10-23 on `generic-rule-instantiation`). Opaque here: the table's type belongs to
     * camel/compile/gir/rule_templates.h, which creates it on first use.
     */
    std::shared_ptr<void> &genericRuleTemplates() { return genericRuleTemplates_; }

    /**
     * The exported entities of the program compiled on this context: the names each module
     * declares for this program, with the graphs this program built (design principles 10.1;
     * one table per program, task). The graph builder of the context's program shares this
     * table; a program of its own (a pass compiled by compileAsProgram) has one of its own, so
     * the same module can be declared in both without either seeing the other's graphs.
     *
     * Created on first use. Readers that are not the graph builder (the semantic analysis, an
     * operator's definition lookup) read it here.
     */
    camel::core::module::ProgramExports &programExports();

    /** Called by loadCmoModule to retain a loaded .cmo handle and prevent unloading. */
    void addLoadedDll(const std::string &path, void *handle) { loadedDllHandles_[path] = handle; }

    module_ptr_t importModule(
        const std::string &rawModuleName, const module::Module *importer = nullptr);
    /// Whether a file for the module is found on the search paths, whether or not it can be
    /// loaded: what tells a name with no module behind it from a module that is there and cannot
    /// be used.
    bool moduleFileExists(const std::string &moduleName);

  private:
    void registerRuntimeGraphDebugInfo(const camel::runtime::Program *program);
};

} // namespace camel::core::context
