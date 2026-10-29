// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "camel/core/context/context.h"
#include "camel/core/type/resolver.h"

#include "camel/core/global_config.h"
#include "camel/core/mm.h"
#include "camel/core/mm/deferred.h"
#include "camel/core/module/builtin.h"
#include "camel/core/module/dynamic.h"
#include "camel/core/module/introspect.h"
#include "camel/core/module/userdef.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/execute/executor.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/definitions.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/log.h"
#include "camel/utils/path.h"
#include "camel/utils/phase_timer.h"
#include "camel/utils/str.h"
#include "core/module/userdef_internal.h"

namespace fs = std::filesystem;
using namespace strutil;
using namespace camel::core::error;
using namespace camel::core::module;
using namespace camel::core::type;

namespace camel::core::context {

namespace {

/// The `Context` objects alive right now, for liveContextCount(): a reading that builds a context
/// in a scope reads it on both sides of that scope to see whether the context was destroyed.
std::atomic<size_t> gLiveContexts{0};

} // namespace

size_t liveContextCount() noexcept {
    return gLiveContexts.load(std::memory_order_acquire);
}

context_ptr_t Context::handle() const {
    // A control block of this object's own with a deleter that does nothing: the handle never
    // keeps the context alive and never frees it, and it is not the control block of the owning
    // pointer, so taking one changes no reference count and the context keeps whatever lifetime
    // its owner gave it (see the declaration in context.h).
    return context_ptr_t(const_cast<Context *>(this), [](Context *) noexcept {});
}

namespace {

/// The exit code the value returned by the entry graph stands for. Only a declared return
/// type makes the value the program's result (design principles 9.8). Under `T!` a value
/// that has not failed counts as its `T`; a failed one is an error nobody handled, and the
/// program ends with a non-zero code (who reports it: camel/execute/entry_args.h).
int deriveProcessExitCode(camel::runtime::GCGraph *graph, slot_t result, bool declared) {
    if (!declared || !graph || !graph->funcType() || !graph->funcType()->hasExitType())
        return 0;

    Type *resultType = graph->funcType()->exitType();
    if (TupleType *fallible = asFallible(resultType)) {
        if (camel::core::rtdata::isErrorValue(result)) {
            return 1;
        }
        result     = camel::core::rtdata::falliblePayload(result);
        resultType = fallible->payloadType();
    }

    switch (resultType->code()) {
    case TypeCode::Int32:
        return static_cast<int>(static_cast<int32_t>(result));
    case TypeCode::Int64:
        return static_cast<int>(static_cast<int64_t>(result));
    case TypeCode::Bool:
        return result != 0 ? 1 : 0;
    case TypeCode::Void:
        return 0;
    default:
        return 0;
    }
}

} // namespace

// ---- the module lookup (camel/core/context/module_resolver.h) -------------------------------
//
// One walk, used by the lookup and by the reason a `ModuleNotFound` carries: the same roots in the
// same order, the same idea of what a module file is. Nothing here reads the environment or the
// command line; the roots arrive as text (design principles 10.2).

bool isModuleName(const std::string &name) {
    if (name.empty())
        return false;
    size_t start = 0;
    for (;;) {
        const size_t dot = name.find('.', start);
        const size_t end = dot == std::string::npos ? name.size() : dot;
        if (end == start)
            return false; // an empty component: a leading dot, or two dots in a row
        for (size_t i = start; i < end; ++i) {
            if (name[i] == '/' || name[i] == '\\')
                return false; // a path separator has no place in a name
        }
        if (dot == std::string::npos)
            return true;
        start = dot + 1;
    }
}

ModuleResolver::ModuleResolver(std::string entryDir, std::vector<std::string> moduleRoots)
    : entryDir_(std::move(entryDir)), moduleRoots_(std::move(moduleRoots)) {}

void ModuleResolver::setModuleRoots(std::vector<std::string> roots) {
    moduleRoots_ = std::move(roots);
    invalidateListings();
}

void ModuleResolver::invalidateListings() {
    listings_.clear();
}

void ModuleResolver::splitLookup(
    const std::string &moduleName, std::string &dirPart, std::string &stem) {
    dirPart = moduleName;
    std::replace(dirPart.begin(), dirPart.end(), '.', '/');
    const size_t slash = dirPart.rfind('/');
    stem               = slash == std::string::npos ? dirPart : dirPart.substr(slash + 1);
    dirPart            = slash == std::string::npos ? std::string() : dirPart.substr(0, slash);
}

std::vector<std::string> ModuleResolver::searchRoots(const std::string &importerDir) const {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    const auto add = [&](const std::string &dir) {
        if (dir.empty())
            return;
        fs::path path = camel::utils::pathFromUtf8(dir);
        if (!path.is_absolute()) {
            // A root written relative is relative to the entry file's directory: the command line
            // means `-I ../shared` the same way wherever the process was started from.
            path = camel::utils::pathFromUtf8(entryDir_) / path;
        }
        const std::string key = camel::utils::pathToUtf8(fs::absolute(path).lexically_normal());
        if (seen.insert(key).second)
            out.push_back(key);
    };
    // The importing module's own directory comes first; the entry program's own directory is
    // `entryDir_` and never a root for another module (design principles 10.2).
    add(importerDir);
    for (const std::string &root : moduleRoots_)
        add(root);
    return out;
}

const ModuleResolver::DirectoryListing &
ModuleResolver::listingFor(const std::string &directory) {
    auto listing = listings_.find(directory);
    if (listing != listings_.end())
        return listing->second;

    DirectoryListing files;
    std::error_code ec;
    const fs::path dir = camel::utils::pathFromUtf8(directory);
    if (fs::is_directory(dir, ec)) {
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec))
                continue;
            const fs::path file = entry.path();
            // Only `.cml` and `.cmo` name a module file. A same-stem file of any other extension
            // (a Markdown document, an editor's leftover) is not a module, and a `.cmo` of the name
            // is what the name stands for.
            const std::string extension = camel::utils::pathToUtf8(file.extension());
            if (extension != ".cml" && extension != ".cmo")
                continue;
            const std::string stem = camel::utils::pathToUtf8(file.stem());
            if (extension == ".cmo") {
                files.cmo.insert(stem);
            } else {
                files.source.try_emplace(stem, camel::utils::pathToUtf8(file));
            }
        }
    }
    return listings_.emplace(directory, std::move(files)).first->second;
}

ModuleFile ModuleResolver::findUnder(const std::string &baseDir, const std::string &moduleName) {
    // A lookup key is a resolved module name, never an import spelling or a path: the relative
    // prefix is resolved against the importing module's name before this, and a leading dot here
    // would turn into a leading '/' below and search the filesystem root.
    if (!isModuleName(moduleName))
        return {};
    std::string dirPart;
    std::string stem;
    splitLookup(moduleName, dirPart, stem);

    const fs::path base = camel::utils::pathFromUtf8(baseDir);
    const fs::path dir  = dirPart.empty() ? base : base / camel::utils::pathFromUtf8(dirPart);

    // Within one directory: `D/xx.cmo`, then `D/xx.cml`; and, when `D/xx` is a directory itself,
    // the same two forms inside it (`D/xx/xx.cmo`, `D/xx/xx.cml`).
    const auto findIn = [&](const fs::path &directory) -> ModuleFile {
        const DirectoryListing &files =
            listingFor(camel::utils::pathToUtf8(directory.lexically_normal()));
        if (files.cmo.contains(stem))
            return {camel::utils::pathToUtf8(directory / (stem + ".cmo")), true};
        if (auto found = files.source.find(stem); found != files.source.end())
            return {found->second, false};
        return {};
    };
    ModuleFile found = findIn(dir);
    if (found.found())
        return found;
    return findIn(dir / camel::utils::pathFromUtf8(stem));
}

ModuleFile
ModuleResolver::resolve(const std::string &moduleName, const std::string &importerDir) {
    camel::utils::phase::Scope phaseScope("resolve", moduleName);
    for (const std::string &root : searchRoots(importerDir)) {
        ModuleFile found = findUnder(root, moduleName);
        if (found.found())
            return found;
    }
    return {};
}

std::string
ModuleResolver::directoryReason(const std::string &directory, const std::string &stem) {
    const fs::path dir = camel::utils::pathFromUtf8(directory);
    std::error_code ec;
    if (!fs::exists(dir, ec))
        return "path does not exist";
    if (!fs::is_directory(dir, ec))
        return "not a directory";
    const DirectoryListing &files = listingFor(camel::utils::pathToUtf8(dir.lexically_normal()));
    if (files.cmo.contains(stem) || files.source.contains(stem))
        return ""; // found: what happened to the file is the caller's to say
    return "no file '" + stem + ".cml' or '" + stem + ".cmo' in directory";
}

std::vector<std::string> ModuleResolver::searchReport(
    const std::string &moduleName, const std::string &importerDir) {
    // Only a resolved module name can name the files to report on; a relative spelling names
    // nothing and is never turned into a path.
    if (!isModuleName(moduleName))
        return {};
    std::string dirPart;
    std::string stem;
    splitLookup(moduleName, dirPart, stem);

    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const std::string &root : searchRoots(importerDir)) {
        const fs::path base = camel::utils::pathFromUtf8(root);
        const fs::path dir  = dirPart.empty() ? base : base / camel::utils::pathFromUtf8(dirPart);
        for (const fs::path &directory :
             {dir.lexically_normal(), (dir / camel::utils::pathFromUtf8(stem)).lexically_normal()}) {
            const std::string key = camel::utils::pathToUtf8(fs::absolute(directory));
            if (!seen.insert(key).second)
                continue;
            const std::string reason = directoryReason(key, stem);
            if (!reason.empty())
                out.push_back(key + ": " + reason);
        }
    }
    return out;
}

size_t hardwareThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 1 : static_cast<size_t>(hardware);
}

Context::Context(const EntryConfig &entryConf, const DiagsConfig &diagConf)
    : entryConfig_(entryConf), diagConfig_(diagConf),
      resolver_(entryConf.entryDir, entryConf.searchPaths) {
    gLiveContexts.fetch_add(1, std::memory_order_acq_rel);
}

size_t Context::threadLimit() const {
    return entryConfig_.maxThreads != 0 ? entryConfig_.maxThreads : hardwareThreadCount();
}

Context::~Context() {
    gLiveContexts.fetch_sub(1, std::memory_order_acq_rel);
    // Runtime function objects stored inside modules may consult GCGraph-backed
    // closure metadata during teardown. Release module-owned runtime data before
    // dropping the graph closure so those metadata pointers stay valid for the
    // whole object destruction cascade.
    facts_.reset();
    definitions_.reset();
    mainModule_.reset();
    modulesByPath_.clear();
    loadedModules_.clear();
    loadedModulePtrs_.clear();
    graphBuildSession_.reset();
    builtinModules_.clear();
    exeMgr_.reset();
    if (runtimeGraphMgr_) {
        // The allocator is one per process and holds one root set and one sweeper per context:
        // this destructor gives back its own, and only its own, so a context that keeps running
        // beside this one keeps its graph roots marked and its graph space swept.
        camel::core::mm::autoSpace().unregisterObjectRootSet(this);
        camel::core::mm::autoSpace().unregisterGraphSweeper(this);
    }
    runtimeGraphMgr_.reset();
// The deferred-release queue is drained here, as the last step. The queue is one per process,
    // and the blocks the last collection found dead carry release callbacks (a DLPack producer's
    // deleter, a Python buffer's release) that no later collection will run when this is the end of
    // the runtime; the destruction above has just queued everything the modules' and the graph
    // manager's own teardown finalized. A context that is not the last one of the process needs
    // this as much as the last one does: a context whose destructor never runs leaves its releases
    // to whoever drains next (mm/deferred.h).
    camel::core::mm::runDeferredReleases();
}

camel::core::module::ProgramExports &Context::programExports() {
    // Created on first use, like the facts and the definition store. Compilation of a context
    // is single-threaded (the graph builder of the program is), so no lock is taken here.
    if (!programExports_) {
        programExports_ = std::make_unique<camel::core::module::ProgramExports>();
    }
    return *programExports_;
}

camel::runtime::DefinitionStore &Context::definitions() {
    // As facts(): created once, on the thread that first asks.
    static std::mutex creation;
    std::lock_guard lock(creation);
    if (!definitions_) {
        definitions_ = std::make_unique<camel::runtime::DefinitionStore>(*this);
    }
    return *definitions_;
}

camel::runtime::fact::FactStore &Context::facts() {
    // Created once, on the thread that first asks: back ends may ask from worker threads.
    static std::mutex creation;
    std::lock_guard lock(creation);
    if (!facts_) {
        (void)camel::runtime::fact::coreKinds(); // the core's kinds exist before any fact
        facts_ = std::make_unique<camel::runtime::fact::FactStore>();
    }
    return *facts_;
}

std::string EntryConfig::toString() const {
    std::ostringstream os;
    os << "{\n";
    os << "  entryDir: " << entryDir << "\n";
    os << "  entryFile: " << entryFile << "\n";
    os << "  searchPaths: [";

    bool first = true;
    for (const auto &path : searchPaths) {
        if (path.empty())
            continue;
        if (!first) {
            os << ", ";
        }
        os << path;
        first = false;
    }

    os << "]\n";
    os << "}";
    return os.str();
}

inline bool fileExists(const std::filesystem::path &path) {
    std::ifstream file(path);
    return file.good();
}

/// The key a module file is cached under: a resolved path, lexically normalized, so two spellings
/// of one file (`a/../b`) are one module and two files are two modules even when they have the
/// same name. Every path a lookup produces is absolute, built from the entry directory and the
/// module roots, so this needs no file-system access (a per-import `weakly_canonical` cost a
/// stat on every component).
std::string canonicalPathKey(const std::string &path) {
    return camel::utils::pathToUtf8(camel::utils::pathFromUtf8(path).lexically_normal());
}

/// True when `name` is a module name a file may be looked up under: see
/// `isModuleName` in camel/core/context/module_resolver.h, which is the one definition the lookup
/// and the semantic analysis both use.

std::optional<module_ptr_t> Context::getBuiltinModule(const std::string &name) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("Context", "Looking for built-in module: <{}>", name));
    auto it = builtinModules_.find(name);
    if (it != builtinModules_.end()) {
        return it->second;
    }

    auto factoryIt = builtinModuleFactories.find(name);
    if (factoryIt != builtinModuleFactories.end()) {
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("Context", "Loading built-in module: <{}>", name));
        camel::utils::phase::Scope phaseScope("builtin_module", name);
        module_ptr_t module = factoryIt->second(shared_from_this());
        module->load(); // instantly load the builtin module
        builtinModules_[name] = module;
        return module;
    }

    return std::nullopt;
}

context_ptr_t Context::create(const EntryConfig &entryConf, const DiagsConfig &diagConf) {
    context_ptr_t ctx     = std::shared_ptr<Context>(new Context(entryConf, diagConf));
    ctx->exeMgr_          = std::make_unique<ExecutorManager>(ctx);
    ctx->runtimeGraphMgr_ = std::make_unique<camel::runtime::GCGraphManager>();
    // Registered under this context's address, so a second context beside this one (a language
    // server, a tool's second context, the Python binding) adds its roots instead of replacing
    // them, and destroying either one gives back only its own.
    camel::core::mm::autoSpace().registerObjectRootSet(
        ctx.get(), "runtimeGraphRoots", &ctx->runtimeGraphMgr_->gcRoots());
    camel::core::mm::autoSpace().registerGraphSweeper(
        ctx.get(), "runtimeGraphSweeper", [manager = ctx->runtimeGraphMgr_.get()] {
            manager->sweepMarked();
        });
    ctx->sourceContext_   = std::make_shared<camel::source::SourceContext>();
    ctx->runtimeDiagSink_ = std::make_shared<DiagnosticSink>("", "", ctx->sourceContext_);
    ctx->runtimeDiagSink_->setConfig(diagConf);
    ctx->runtimeErrorReporter_ =
        std::make_shared<RuntimeErrorReporter>(ctx->runtimeDiagSink_, ctx->sourceContext_);
    {
        // The globals module is the built-in module the empty name stands for; it is part of the
        // imported set from the start (`importedModules`).
        module_ptr_t globals = ctx->getBuiltinModule("").value();
        ctx->loadedModules_.push_back(globals);
        ctx->loadedModulePtrs_.insert(globals.get());
    }
    {
        size_t nPaths = 0;
        for (const auto &sp : entryConf.searchPaths) {
            if (!sp.empty())
                ++nPaths;
        }
        CAMEL_LOG_INFO_S(
            "Context",
            "run | context | entry={} | cwd={} | module_path_slots={}",
            entryConf.entryFile,
            entryConf.entryDir,
            nPaths);
    }
    return ctx;
}

namespace {

/// A diagnostic placed by token range (the GCT builder's) only gets a line and column once the
/// range is converted with the module's token stream; without this it prints as `?:?`.
void resolveTokenRanges(const std::shared_ptr<UserDefinedModule> &module) {
    if (module && module->diagnostics() && module->parser()) {
        module->diagnostics()->fetchAll(module->parser()->tokenPositions());
    }
}

} // namespace

size_t Context::dumpModuleWarnings(
    std::ostream &os, bool json, const std::vector<Diagnostic> &extra) const {
    std::vector<std::string> lines;
    for (const auto &mod : allUserModules()) {
        auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
        if (!ud || !ud->diagnostics() || ud->diagnostics()->hasErrors()) {
            continue;
        }
        resolveTokenRanges(ud);
        for (const Diagnostic *diag : ud->diagnostics()->findBySeverity(Severity::Warn)) {
            if (admitShownWarning()) {
                lines.push_back(json ? diag->toJson() : diag->toText(os));
            }
        }
    }
    for (const Diagnostic &diag : extra) {
        if (admitShownWarning()) {
            lines.push_back(json ? diag.toJson() : diag.toText(os));
        }
    }
    if (lines.empty()) {
        return 0;
    }
    if (json) {
        os << "[\n" << join(lines, ",\n") << "\n]\n";
    } else {
        const bool rich = camel::core::error::diagnosticFormat() == DiagFormat::Rich;
        os << join(lines, rich ? "\n\n" : "\n") << (rich ? "\n\n" : "\n");
    }
    os << std::flush;
    return lines.size();
}

void Context::dumpAllModuleDiagnostics(std::ostream &os, bool json, bool wrapInArray) const {
    dumpModuleDiagnostics(os, json, wrapInArray, allUserModules());
}

void Context::dumpModuleDiagnostics(
    std::ostream &os, bool json, bool wrapInArray,
    const std::vector<module_ptr_t> &modules) const {
    auto allMods = allUserModules();
    std::vector<module_ptr_t> modsErr;
    for (const auto &mod : modules) {
        auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
        if (ud && ud->diagnostics() && ud->diagnostics()->hasErrors()) {
            resolveTokenRanges(ud);
            modsErr.push_back(mod);
        }
    }
    // An imported module's errors come before those of the entry module, which may follow
    // from them (ImportedModuleHasErrors).
    std::stable_partition(modsErr.begin(), modsErr.end(), [&](const module_ptr_t &mod) {
        return mod != mainModule_;
    });
    if (json) {
        if (modsErr.empty()) {
            if (wrapInArray) {
                os << "[]\n" << std::flush;
            }
        } else {
            if (wrapInArray) {
                os << "[\n";
            }
            bool first = true;
            for (const auto &mod : modsErr) {
                auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
                if (!ud || !ud->diagnostics())
                    continue;
                if (!first)
                    os << ",\n";
                ud->diagnostics()->dump(os, true, false);
                first = false;
            }
            if (wrapInArray) {
                os << "\n]\n" << std::flush;
            }
        }
    } else {
        for (const auto &mod : modsErr) {
            auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
            if (!ud || !ud->diagnostics())
                continue;
            if (mod != mainModule_) {
                std::vector<std::string> importers;
                for (const auto &other : allMods) {
                    if (other != mod && other->imports(mod)) {
                        importers.push_back(other->name());
                    }
                }
                if (!importers.empty()) {
                    os << "[imported by " << join(importers, ", ") << "]\n";
                }
            }
            ud->diagnostics()->dump(os, false);
            if (camel::core::error::diagnosticFormat() == DiagFormat::Rich && mod != modsErr.back()) {
                os << "\n";
            }
        }
        if (modsErr.empty()) {
            os << "[camel] Compilation failed but no diagnostics were collected. Try `npm run "
                  "debug` for assertions, or verify camel.exe and libcamel.dll are the same build "
                  "flavor (Release vs Debug).\n"
               << std::flush;
        }
    }
}

void Context::captureProcessExitCode(camel::runtime::GCGraph *graph, slot_t result) {
    processExitCode_ = deriveProcessExitCode(graph, result, entryResultDeclared_);
    if (entryResultHandler_) {
        entryResultHandler_(graph, result);
    }
}

std::vector<module_ptr_t> Context::importedModules() const {
    std::vector<module_ptr_t> result = loadedModules_;
    std::sort(result.begin(), result.end(), [](const module_ptr_t &a, const module_ptr_t &b) {
        return a->name() < b->name();
    });
    return result;
}

std::vector<module_ptr_t> Context::allUserModules() const {
    std::vector<module_ptr_t> result;
    std::unordered_set<module_ptr_t> seen;

    if (mainModule_) {
        if (std::dynamic_pointer_cast<UserDefinedModule>(mainModule_)) {
            result.push_back(mainModule_);
            seen.insert(mainModule_);
        }
    }

    for (const auto &mod : loadedModules_) {
        if (seen.count(mod))
            continue;
        if (std::dynamic_pointer_cast<UserDefinedModule>(mod)) {
            result.push_back(mod);
            seen.insert(mod);
        }
    }
    return result;
}

module_ptr_t
Context::importModule(const std::string &rawModuleName, const module::Module *importer) {
    if (rawModuleName.empty()) {
        return getBuiltinModule("").value_or(nullptr); // builtin module already loaded
    }

    const std::string importerName = importer != nullptr ? importer->name() : std::string();
    CAMEL_LOG_INFO_S(
        "Context",
        "Importing module '{}' from current module '{}'.",
        rawModuleName,
        importerName);
    lastCmoLoadError_.clear();
    std::string nameProblem;
    auto candidates = getModuleNameCandidates(importerName, rawModuleName, nameProblem);
    if (!nameProblem.empty()) {
        // The spelling is a name the grammar accepts, but against this importer it names nothing.
        // It is reported as not found and never becomes a path to search.
        throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound)
            .commit(rawModuleName, nameProblem + "\n");
    }

    // Design principles 10.2: a module's import looks in its own directory first, then the module
    // roots, then the standard library, and the entry program follows the same rule. The entry
    // directory is therefore its own directory and never a root for another module: a library's
    // import resolves to the same place whoever imports it. Without that, the standard
    // library's own `import graph` in stdlib/rules.cml could find a same-named program of the
    // entry directory instead of stdlib/graph.cmo.
    const std::string importerDir = importerDirectory(importer);

    for (const auto &name : candidates) {
        auto [path, isCmo] = resolveModulePath(name, importerDir);
        if (!path.empty()) {
            if (auto cached = moduleAtPath(path)) {
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "Context", "Module '{}' found in cache ({}).", name, path));
                // Also a module being compiled right now: modules may import each other (design
                // principles 10.1). Its declarations are complete before any function body of the
                // program is built.
                return cached;
            }
            module_ptr_t module = loadModuleFrom(name, path, isCmo);
            if (auto accepted = acceptModule(std::move(module), path, name)) {
                return accepted;
            }
            // The file was found and it is not a usable module (a `.cmo` the loader refused: an
            // ABI mismatch, a missing symbol, a missing dependency, a file that is not a library at
            // all). The name stands for that file, and a module that is there and cannot be used is
            // an error: no other candidate name is tried, and no built-in module takes its place
            // (the import ends below with `lastCmoLoadError_` as the reason).
            break;
        }

        auto builtin = getBuiltinModule(name);
        if (builtin.has_value()) {
            EXEC_WHEN_DEBUG(
                CAMEL_LOG_DEBUG_S("Context", "Module '{}' found in built-in modules.", name));
            if (auto accepted = acceptModule(builtin.value(), "", name)) {
                return accepted;
            }
        }
    }

    std::string firstCandidate = candidates.empty() ? rawModuleName : candidates.front();
    std::ostringstream reason;
    bool hadCmoLoadError = !lastCmoLoadError_.empty();
    if (hadCmoLoadError) {
        reason << "Found .cmo but load failed: " << lastCmoLoadError_;
        lastCmoLoadError_.clear();
        std::vector<std::string> pathBases = resolver_.searchRoots(importerDir);
        if (!pathBases.empty()) {
            reason << " Search paths tried:";
            for (const auto &p : pathBases)
                reason << "\n  " << p;
        }
    } else {
        std::vector<std::string> diagLines = resolver_.searchReport(firstCandidate, importerDir);
        if (!diagLines.empty()) {
            reason << "Search attempts:";
            for (const auto &line : diagLines)
                reason << "\n  " << line;
        }
        if (candidates.size() > 1) {
            if (reason.tellp() > 0)
                reason << " ";
            reason << "Also tried module name(s): ";
            for (size_t i = 1; i < candidates.size(); ++i)
                reason << (i > 1 ? ", " : "") << "'" << candidates[i] << "'";
        }
    }
    std::string reasonStr = reason.str();
    if (reasonStr.empty())
        reasonStr = "No matching module file or builtin.";
    while (!reasonStr.empty() && reasonStr.back() == '\n')
        reasonStr.pop_back();
    reasonStr += "\n";
    throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound).commit(rawModuleName, reasonStr);
}

void Context::claimBuiltinSignature(
    const std::shared_ptr<camel::core::type::FuncTypeResolver> &rule,
    camel::core::type::FunctionType *signature) {
    for (auto &[claimed, signatures] : builtinClaims_) {
        if (claimed == rule) {
            signatures.push_back(signature);
            return;
        }
    }
    builtinClaims_.push_back({rule, {signature}});
}

std::string Context::checkPreludeDeclarations() {
    if (preludeChecked_) {
        return preludeProblem_;
    }
    preludeChecked_ = true;
    for (const std::string &root : resolver_.searchRoots(std::string())) {
        const fs::path file = camel::utils::pathFromUtf8(root) / "prelude.cml";
        std::error_code ec;
        if (!fs::is_regular_file(file, ec) || ec) {
            continue;
        }
        const std::string path = camel::utils::pathToUtf8(file);
        auto prelude = std::static_pointer_cast<UserDefinedModule>(
            UserDefinedModule::fromFile("prelude", path, shared_from_this()));
        preludeModule_ = prelude;
        try {
            prelude->compile(CompileStage::Declared);
        } catch (...) {
            // A file that does not parse is reported by its own diagnostics, below.
            if (!prelude->diagnostics() || !prelude->diagnostics()->hasErrors()) {
                throw;
            }
        }
        if (prelude->diagnostics() && prelude->diagnostics()->hasErrors()) {
            preludeProblem_ = path;
        } else {
            for (const auto &[rule, signatures] : builtinClaims_) {
                if (signatures.size() == 1) {
                    rule->graftDeclaredSignature(signatures.front());
                }
            }
        }
        builtinClaims_.clear();
        return preludeProblem_;
    }
    return preludeProblem_;
}

std::string Context::importerDirectory(const module::Module *importer) const {
    if (importer != nullptr && !importer->path().empty()) {
        return camel::utils::pathToUtf8(
            camel::utils::pathFromUtf8(importer->path()).parent_path());
    }
    // The entry program (and a program-level import, which stands where the program does): its
    // own directory is the entry file's directory.
    if (mainModule_ && !mainModule_->path().empty()) {
        return camel::utils::pathToUtf8(
            camel::utils::pathFromUtf8(mainModule_->path()).parent_path());
    }
    return entryConfig_.entryDir;
}

module_ptr_t Context::moduleAtPath(const std::string &path) const {
    auto it = modulesByPath_.find(canonicalPathKey(path));
    return it == modulesByPath_.end() ? nullptr : it->second;
}

module_ptr_t Context::acceptModule(
    module_ptr_t module, const std::string &resolvedPath, const std::string &importName) {
    if (!module)
        return nullptr;
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "Context",
        "Module '{}' loaded from file '{}'.",
        importName,
        module->path()));
    // Register before load so compile failures remain visible to dumpAllModuleDiagnostics. The
    // cache is keyed by the file the name resolved to, not by the name: two modules in
    // different directories that import the same name get the file beside each of them.
    if (!resolvedPath.empty()) {
        modulesByPath_[canonicalPathKey(resolvedPath)] = module;
    }
    if (module != mainModule_ && loadedModulePtrs_.insert(module.get()).second) {
        loadedModules_.push_back(module);
    }
    // A module written in Camel is compiled by the module that imports it, as far as
    // its declarations, once that module has bound its imports and declared its types
    // (GCT::Builder::visitModule), so that modules can import each other.
    if (!module->loaded() && !std::dynamic_pointer_cast<UserDefinedModule>(module)) {
        bool loadedOk = false;
        camel::utils::phase::Scope phaseScope("import", importName);
        loadedOk = module->load();
        if (!loadedOk) {
            // A module that was found and is in error is not a module that was not
            // found: its own diagnostics say what is wrong with it.
            auto ud = std::dynamic_pointer_cast<UserDefinedModule>(module);
            if (ud && ud->diagnostics() && ud->diagnostics()->hasErrors()) {
                throw DiagnosticBuilder::of(SemanticDiag::ImportedModuleHasErrors)
                    .commit(importName, module->path());
            }
            throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound)
                .commit(importName, "failed to compile or load module from " + module->path());
        }
    }
    // The purity of an operator must be declared (design principles 3.3). A module
    // that leaves it out still works, with its operators treated as having side
    // effects; it is told so. The modules of this repository are held to it by a
    // test (test/plans/feat/modules/stdlib_registry.plan.toml).
    if (module->loaded() && !std::dynamic_pointer_cast<UserDefinedModule>(module)) {
        camel::core::module::warnUndeclaredOperatorTraits(*module);
    }
    return module;
}

std::vector<std::string> Context::getModuleNameCandidates(
    const std::string &currentModule, const std::string &rawImportName, std::string &problem) {
    problem.clear();
    std::vector<std::string> candidates;

    if (rawImportName.empty())
        return std::vector<std::string>({""});

    if (rawImportName[0] == '.') {
        // A relative import spelling is resolved against the importing module's own name, not
        // against its directory (design principles 10.2: an import names a module, not a path).
        // When the prefix drops more components than the name has, the spelling names nothing:
        // the import fails here, before any lookup, rather than becoming a path.
        std::string resolved = resolveRelativeModuleName(currentModule, rawImportName, problem);
        if (problem.empty())
            candidates.push_back(std::move(resolved));
    } else {
        candidates.push_back(rawImportName);

        // relative-to-parent fallback
        auto base = split(currentModule, '.');
        for (int i = base.size(); i >= 0; --i) {
            std::vector<std::string> prefix(base.begin(), base.begin() + i);
            prefix.push_back(rawImportName);
            candidates.push_back(join(prefix, "."));
        }
    }

    return candidates;
}

std::string Context::resolveRelativeModuleName(
    const std::string &currentModule, const std::string &importName, std::string &problem) {
    problem.clear();

    int level = 0;
    size_t i  = 0;
    while (i < importName.size() && importName[i] == '.') {
        ++level;
        ++i;
    }

    std::string remaining = importName.substr(i);
    auto base             = split(currentModule, '.');

    if (static_cast<size_t>(level) > base.size()) {
        // The dots count components of the importing module's name to drop; with fewer components
        // than dots there is no name to form. This is not "not found in a directory": no import
        // and no file lookup exists, so the diagnostic says exactly that.
        problem = std::format(
            "The relative module name '{}' drops {} name component(s), and the importing module's "
            "name '{}' has {}: no module name can be formed, so no directory was searched. An "
            "import names a module: the dots drop components of the importing module's name, they "
            "do not walk the directory tree.",
            importName,
            level,
            currentModule,
            base.size());
        return {};
    }

    base.resize(base.size() - level);

    if (!remaining.empty()) {
        auto rest = split(remaining, '.');
        base.insert(base.end(), rest.begin(), rest.end());
    }

    return join(base, ".");
}

void Context::setModuleSearchPaths(std::vector<std::string> roots) {
    // The roots of record and the roots the lookup uses are one list, changed in one place: a
    // caller cannot leave the two disagreeing, and the lookup cannot answer the new list out of
    // the old list's directory listings (ModuleResolver::setModuleRoots drops them).
    entryConfig_.searchPaths = std::move(roots);
    resolver_.setModuleRoots(entryConfig_.searchPaths);
}

std::pair<std::string, bool> Context::resolveModulePath(
    const std::string &moduleName, const std::string &importerDir) {
    // The one lookup: the importing module's own directory, then the module roots, in the order
    // `ModuleResolver::searchRoots` gives (design principles 10.2). A hit is what the name stands
    // for, and only `.cml` and `.cmo` name a module file.
    ModuleFile found = resolver_.resolve(moduleName, importerDir);
    return {found.path, found.isCmo};
}

std::string Context::getModulePath(const std::string &moduleName) {
    return resolveModulePath(moduleName, importerDirectory(nullptr)).first;
}

bool Context::moduleFileExists(const std::string &moduleName) {
    return !resolveModulePath(moduleName, importerDirectory(nullptr)).first.empty();
}

module_ptr_t
Context::loadModuleFrom(const std::string &moduleName, const std::string &path, bool isCmo) {
    if (path.empty())
        return nullptr;
    if (isCmo) {
        camel::utils::phase::Scope phaseScope("cmo_load", moduleName);
        std::string err;
        module_ptr_t mod = loadCmoModule(moduleName, path, shared_from_this(), &err);
        if (!mod && !err.empty())
            lastCmoLoadError_ = std::move(err);
        if (!mod)
            return mod;
        // A `.cml` beside the `.cmo` is the declaration module of that module (design
        // principles 6.4, step 1): the C++ module implements its operators, and
        // the `.cml` is the single source of their signatures. It is not a shadow of the
        // `.cmo` — the `.cmo` is still what the lookup found and what the name stands for —
        // and it is not dead code.
        const fs::path declarations = camel::utils::pathFromUtf8(path).replace_extension(".cml");
        std::error_code ec;
        if (fs::is_regular_file(declarations, ec) && !ec) {
            // The declarations are checked against the executors and the operator traits of the
            // module they declare, which its own load registers. It loads once: this is the
            // only place the module is loaded before it is asked for an operator.
            if (!mod->load()) {
                return mod;
            }
            camel::core::module::warnUndeclaredOperatorTraits(*mod);
            auto declared = std::static_pointer_cast<UserDefinedModule>(
                UserDefinedModule::fromFile(
                    moduleName, camel::utils::pathToUtf8(declarations), shared_from_this()));
            declared->setDeclarationModule(true);
            declared->setImplementation(mod);
            // One module, not two (design principles 6.4): what the
            // C++ module exports — its types, its constants and the groups and tags it
            // declares — is what this module exports, and a `.cml` name is written into the
            // same tables. An importer of the name never learns there were two objects.
            declared->inheritExportsFrom(*mod);
            return declared;
        }
        return mod;
    }
    // The file of the entry module (which is registered under no module name): the entry
    // itself, not a second copy of it. A module may import itself, to no effect (design
    // principles 10.1), and another module may import the entry back.
    if (mainModule_ && !mainModule_->path().empty()) {
        std::error_code ec;
        if (fs::equivalent(
                camel::utils::pathFromUtf8(path),
                camel::utils::pathFromUtf8(mainModule_->path()),
                ec) &&
            !ec) {
            return mainModule_;
        }
    }
    return UserDefinedModule::fromFile(moduleName, path, shared_from_this());
}

const camel::runtime::Program *Context::runtimeProgram() {
    return currentRuntimeProgram() ? currentRuntimeProgram() : materializeRuntimeProgram();
}

const camel::runtime::Program *Context::materializeRuntimeProgram() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->clear();
    ASSERT(mainModule_ != nullptr, "Main module is not set in context.");
    auto rootModule = tt::as_shared<UserDefinedModule>(mainModule_);
    camel::utils::phase::Scope phaseScope("encode");
    camel::runtime::Program *program = runtimeGraphMgr_->reserve();
    camel::core::module::detail::UserDefinedModuleAccess::encodeRuntimeProgram(
        *rootModule,
        *program);
    runtimeGraphMgr_->adoptRoot(program);
    return program;
}

const camel::runtime::Program *
Context::installRuntimeProgram(const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->replaceRoot(program);
    return program;
}

const camel::runtime::Program *
Context::adoptRuntimeProgram(const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->adoptRoot(program);
    return program;
}

bool Context::installRuntimeProgramIfCurrent(
    const camel::runtime::Program *expected, const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    if (!runtimeGraphMgr_->replaceRootIfCurrent(expected, program)) {
        return false;
    }
    return true;
}

const camel::runtime::Program *Context::currentRuntimeProgram() const {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    return runtimeGraphMgr_->root();
}

void Context::clearRuntimeGraphs() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->clear();
}

camel::runtime::Program *Context::reserveProgram() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    return runtimeGraphMgr_->reserve();
}

void Context::trackDetachedRuntimeProgram(const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->trackDetached(program);
}

void Context::settleRuntimeGraphs(const camel::runtime::Program *keep) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->settle(keep);
}

void Context::collectRuntimeGraphs() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->collect();
}

void Context::rootProgramForRun(const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->rootForRun(program);
}

void Context::unrootProgramForRun(const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->unrootForRun(program);
}

void Context::enterRun() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->enterRun();
}

void Context::leaveRun() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->leaveRun();
}

void Context::releaseRuntimeGraph(camel::runtime::GCGraph *graph) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->release(graph);
}

void Context::noteUnboundRuntimeGraph(camel::runtime::GCGraph *graph) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->noteUnbound(graph);
}

const camel::runtime::Program *
Context::programOfRuntimeGraph(const camel::runtime::GCGraph *graph) const {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    return runtimeGraphMgr_->programOf(graph);
}

void Context::pinRuntimeProgram(const camel::runtime::Program *program) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->pin(program);
}

void Context::releaseDetachedRuntimeGraphs() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->releaseDetached();
}

void Context::registerExecutorFactory(std::string name, executor_factory_t fact) {
    exeMgr_->registerExecutorFactory(name, fact);
}

} // namespace camel::core::context
