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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <tuple>
#include <unordered_set>

#include "camel/core/context/context.h"
#include "camel/core/module/introspect.h"
#include "camel/core/mm.h"
#include "camel/core/module/builtin.h"
#include "camel/core/module/dynamic.h"
#include "camel/core/module/userdef.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/execute/executor.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/log.h"
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

/// The exit code the value returned by the entry graph stands for. Only a declared return
/// type makes the value the program's result (design principles 2.10). Under `T!` a value
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
    case TypeCode::Byte:
        return static_cast<int>(static_cast<uint8_t>(result));
    case TypeCode::Void:
        return 0;
    default:
        return 0;
    }
}

} // namespace

Context::Context(const EntryConfig &entryConf, const DiagsConfig &diagConf)
    : entryConfig_(entryConf), diagConfig_(diagConf) {}

Context::~Context() {
    // Runtime function objects stored inside modules may consult GCGraph-backed
    // closure metadata during teardown. Release module-owned runtime data before
    // dropping the graph closure so those metadata pointers stay valid for the
    // whole object destruction cascade.
    mainModule_.reset();
    modules_.clear();
    graphBuildSession_.reset();
    builtinModules_.clear();
    exeMgr_.reset();
    if (runtimeGraphMgr_) {
        camel::core::mm::autoSpace().setObjectRootSet(nullptr);
    }
    runtimeGraphMgr_.reset();
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

inline bool fileExists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

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
    camel::core::mm::autoSpace().setObjectRootSet(&ctx->runtimeGraphMgr_->gcRoots());
    ctx->sourceContext_   = std::make_shared<camel::source::SourceContext>();
    ctx->runtimeDiagSink_ = std::make_shared<DiagnosticSink>("", "", ctx->sourceContext_);
    ctx->runtimeDiagSink_->setConfig(diagConf);
    ctx->runtimeErrorReporter_ =
        std::make_shared<RuntimeErrorReporter>(ctx->runtimeDiagSink_, ctx->sourceContext_);
    ctx->modules_[""] = ctx->getBuiltinModule("").value();
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

void Context::dumpAllModuleDiagnostics(std::ostream &os, bool json) const {
    auto allMods = allUserModules();
    std::vector<module_ptr_t> modsErr;
    for (const auto &mod : allMods) {
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
            os << "[]\n" << std::flush;
        } else {
            os << "[\n";
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
            os << "\n]\n" << std::flush;
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
    std::vector<module_ptr_t> result;
    for (const auto &[name, mod] : modules_) {
        if (mod) {
            result.push_back(mod);
        }
    }
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

    for (const auto &[name, mod] : modules_) {
        if (name.empty())
            continue;
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
Context::importModule(const std::string &rawModuleName, const std::string &currentModuleName) {
    if (rawModuleName.empty()) {
        return modules_[""]; // builtin module already loaded
    }

    CAMEL_LOG_INFO_S(
        "Context",
        "Importing module '{}' from current module '{}'.",
        rawModuleName,
        currentModuleName);
    lastCmoLoadError_.clear();
    auto candidates = getModuleNameCandidates(currentModuleName, rawModuleName);

    for (const auto &name : candidates) {
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("Context", "Module '{}' found in cache.", name));
            // The module being loaded names itself (a file `m.cml` with `import m`): it is
            // in the cache, registered before its load, and not yet a module. Handing it out
            // gave the importer a module without graphs, and the compiler crashed.
            if (name == currentModuleName && !it->second->loaded() &&
                std::dynamic_pointer_cast<UserDefinedModule>(it->second)) {
                throw DiagnosticBuilder::of(SemanticDiag::ImportCycle)
                    .commit(name, "file " + it->second->path());
            }
            // A module still being loaded is imported again by a module it imports
            // (a imports b, b imports a; or a longer ring). It has no graphs yet, and handing
            // it out crashed the compiler. Mutual import is not supported: say which ring.
            if (auto at = std::find(loadingModules_.begin(), loadingModules_.end(), name);
                at != loadingModules_.end()) {
                std::string chain = "import cycle: ";
                for (auto m = at; m != loadingModules_.end(); ++m) {
                    chain += *m + " -> ";
                }
                throw DiagnosticBuilder::of(SemanticDiag::ImportCycle).commit(name, chain + name);
            }
            return it->second;
        }

        module_ptr_t module = tryLoadModule(name);
        if (module) {
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "Context",
                "Module '{}' loaded from file '{}'.",
                name,
                module->path()));
            // Register before load so compile failures remain visible to dumpAllModuleDiagnostics.
            modules_[name] = module;
            if (!module->loaded()) {
                loadingModules_.push_back(name);
                bool loadedOk = false;
                camel::utils::phase::Scope phaseScope("import", name);
                try {
                    loadedOk = module->load();
                } catch (...) {
                    loadingModules_.pop_back();
                    throw;
                }
                loadingModules_.pop_back();
                if (!loadedOk) {
                    // A module that was found and is in error is not a module that was not
                    // found: its own diagnostics say what is wrong with it.
                    auto ud = std::dynamic_pointer_cast<UserDefinedModule>(module);
                    if (ud && ud->diagnostics() && ud->diagnostics()->hasErrors()) {
                        throw DiagnosticBuilder::of(SemanticDiag::ImportedModuleHasErrors)
                            .commit(name, module->path());
                    }
                    throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound)
                        .commit(name, "failed to compile or load module from " + module->path());
                }
            }
            // The purity of an operator must be declared (design principles 2.9). A module
            // that leaves it out still works, with its operators treated as having side
            // effects; it is told so. The modules of this repository are held to it by a
            // test (test/plans/feat/modules/stdlib_registry.plan.toml).
            if (module->loaded() && !std::dynamic_pointer_cast<UserDefinedModule>(module)) {
                camel::core::module::warnUndeclaredOperatorTraits(*module);
            }
            return module;
        }

        auto builtin = getBuiltinModule(name);
        if (builtin.has_value()) {
            EXEC_WHEN_DEBUG(
                CAMEL_LOG_DEBUG_S("Context", "Module '{}' found in built-in modules.", name));
            modules_[name] = builtin.value();
            return builtin.value();
        }
    }

    std::string firstCandidate = candidates.empty() ? rawModuleName : candidates.front();
    std::ostringstream reason;
    bool hadCmoLoadError = !lastCmoLoadError_.empty();
    if (hadCmoLoadError) {
        reason << "Found .cmo but load failed: " << lastCmoLoadError_;
        lastCmoLoadError_.clear();
        std::vector<std::string> pathBases = getSearchPathBases();
        if (!pathBases.empty()) {
            reason << " Search paths tried:";
            for (const auto &p : pathBases)
                reason << "\n  " << p;
        }
    } else {
        std::vector<std::string> diagLines = getModuleSearchDiagnostics(firstCandidate);
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

std::vector<std::string> Context::getModuleNameCandidates(
    const std::string &currentModule, const std::string &rawImportName) {
    std::vector<std::string> candidates;

    if (rawImportName.empty())
        return std::vector<std::string>({""});

    if (rawImportName[0] == '.') {
        try {
            std::string resolved = resolveRelativeModuleName(currentModule, rawImportName);
            candidates.push_back(resolved);
        } catch (...) {
            // ignore invalid relative import
        }
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
    const std::string &currentModule, const std::string &importName) {

    int level = 0;
    size_t i  = 0;
    while (i < importName.size() && importName[i] == '.') {
        ++level;
        ++i;
    }

    std::string remaining = importName.substr(i);
    auto base             = split(currentModule, '.');

    if (static_cast<size_t>(level) > base.size()) {
        throw DiagnosticBuilder::of(RuntimeDiag::RuntimeError)
            .commit("Too many dots in relative import: " + importName);
    }

    base.resize(base.size() - level);

    if (!remaining.empty()) {
        auto rest = split(remaining, '.');
        base.insert(base.end(), rest.begin(), rest.end());
    }

    return join(base, ".");
}

std::pair<std::string, bool> Context::getModulePathAndKind(const std::string &moduleName) {
    std::string relativePath = moduleName;
    std::replace(relativePath.begin(), relativePath.end(), '.', '/');
    fs::path relPath(relativePath);
    fs::path parentDir = relPath.parent_path();
    std::string stem   = relPath.filename().string();
    if (stem.empty())
        stem = relPath.string();

    /// Lookup order within directory D: 1) D/xx.cmo  2) D/xx.* (any text/source file with the
    /// same name)
    /// If D/xx is a directory, also search: 3) D/xx/xx.cmo  4) D/xx/xx.*
    auto findModuleInDir = [&](const fs::path &dir) -> std::pair<std::string, bool> {
        if (!fs::exists(dir) || !fs::is_directory(dir))
            return {"", false};
        fs::path cmoPath = dir / (stem + ".cmo");
        if (fileExists(cmoPath.string()))
            return {cmoPath.string(), true};
        for (const auto &entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file())
                continue;
            fs::path p = entry.path();
            if (p.stem() == stem)
                return {p.string(), (p.extension() == ".cmo")};
        }
        return {"", false};
    };

    auto tryDir = [&](const fs::path &base) -> std::pair<std::string, bool> {
        fs::path dir       = base / parentDir;
        auto [path, isCmo] = findModuleInDir(dir);
        if (!path.empty())
            return {path, isCmo};
        fs::path subdir = dir / stem;
        return findModuleInDir(subdir);
    };

    for (const auto &dir : entryConfig_.searchPaths) {
        if (dir.empty())
            continue;
        fs::path basePath = fs::path(dir);
        if (!basePath.is_absolute())
            basePath = fs::path(entryConfig_.entryDir) / basePath;
        auto [path, isCmo] = tryDir(basePath);
        if (!path.empty())
            return {path, isCmo};
    }

    auto [path, isCmo] = tryDir(fs::path(entryConfig_.entryDir));
    if (!path.empty())
        return {path, isCmo};
    return {"", false};
}

std::vector<std::string> Context::getModuleSearchDiagnostics(const std::string &moduleName) const {
    std::string relativePath = moduleName;
    std::replace(relativePath.begin(), relativePath.end(), '.', '/');
    fs::path relPath(relativePath);
    fs::path parentDir = relPath.parent_path();
    std::string stem   = relPath.filename().string();
    if (stem.empty())
        stem = relPath.string();

    std::vector<std::string> out;

    auto reasonInDir = [&](const fs::path &dir) -> std::string {
        if (!fs::exists(dir))
            return "path does not exist";
        if (!fs::is_directory(dir))
            return "not a directory";
        fs::path cmoPath = dir / (stem + ".cmo");
        if (fileExists(cmoPath.string()))
            return ""; // found, caller should not use this for failed attempt
        for (const auto &entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file())
                continue;
            fs::path p = entry.path();
            if (p.stem() == stem)
                return ""; // found
        }
        return "no file '" + stem + ".cmo' or '" + stem + ".*' in directory";
    };

    auto tryDirAndRecord = [&](const fs::path &base) {
        fs::path dir       = base / parentDir;
        std::string reason = reasonInDir(dir);
        if (!reason.empty())
            out.push_back(fs::absolute(dir).string() + ": " + reason);
        fs::path subdir       = dir / stem;
        std::string subReason = reasonInDir(subdir);
        if (!subReason.empty())
            out.push_back(fs::absolute(subdir).string() + ": " + subReason);
    };

    for (const auto &dir : entryConfig_.searchPaths) {
        if (dir.empty())
            continue;
        fs::path basePath = fs::path(dir);
        if (!basePath.is_absolute())
            basePath = fs::path(entryConfig_.entryDir) / basePath;
        tryDirAndRecord(basePath);
    }
    tryDirAndRecord(fs::path(entryConfig_.entryDir));

    return out;
}

std::vector<std::string> Context::getSearchPathBases() const {
    std::vector<std::string> out;
    for (const auto &dir : entryConfig_.searchPaths) {
        if (dir.empty())
            continue;
        fs::path basePath = fs::path(dir);
        if (!basePath.is_absolute())
            basePath = fs::path(entryConfig_.entryDir) / basePath;
        out.push_back(fs::absolute(basePath).string());
    }
    out.push_back(fs::absolute(fs::path(entryConfig_.entryDir)).string());
    return out;
}

std::string Context::getModulePath(const std::string &moduleName) {
    return getModulePathAndKind(moduleName).first;
}

bool Context::moduleFileExists(const std::string &moduleName) {
    return !getModulePathAndKind(moduleName).first.empty();
}

module_ptr_t Context::tryLoadModule(const std::string &moduleName) {
    std::string path;
    bool isCmo = false;
    {
        camel::utils::phase::Scope phaseScope("resolve", moduleName);
        std::tie(path, isCmo) = getModulePathAndKind(moduleName);
    }
    if (path.empty())
        return nullptr;
    if (isCmo) {
        camel::utils::phase::Scope phaseScope("cmo_load", moduleName);
        std::string err;
        module_ptr_t mod = loadCmoModule(moduleName, path, shared_from_this(), &err);
        if (!mod && !err.empty())
            lastCmoLoadError_ = std::move(err);
        return mod;
    }
    return UserDefinedModule::fromFile(moduleName, path, shared_from_this());
}

camel::runtime::GCGraph *Context::runtimeRootGraph() {
    return currentRuntimeRoot() ? currentRuntimeRoot() : materializeRuntimeRoot();
}

camel::runtime::GCGraph *Context::materializeRuntimeRoot() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->clear();
    ASSERT(mainModule_ != nullptr, "Main module is not set in context.");
    auto rootModule = tt::as_shared<UserDefinedModule>(mainModule_);
    camel::utils::phase::Scope phaseScope("encode");
    camel::runtime::GCGraph *runtimeRoot =
        camel::core::module::detail::UserDefinedModuleAccess::encodeRuntimeGraph(*rootModule);
    runtimeGraphMgr_->adoptRoot(runtimeRoot);
    registerRuntimeGraphDebugInfo(runtimeRoot);
    return runtimeRoot;
}

camel::runtime::GCGraph *Context::installRuntimeRoot(camel::runtime::GCGraph *runtimeRoot) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->replaceRoot(runtimeRoot);
    registerRuntimeGraphDebugInfo(runtimeRoot);
    return runtimeRoot;
}

camel::runtime::GCGraph *Context::adoptRuntimeRoot(camel::runtime::GCGraph *runtimeRoot) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->adoptRoot(runtimeRoot);
    registerRuntimeGraphDebugInfo(runtimeRoot);
    return runtimeRoot;
}

camel::runtime::GCGraph *Context::currentRuntimeRoot() const {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    return runtimeGraphMgr_->root();
}

void Context::clearRuntimeGraphs() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->clear();
}

void Context::trackDetachedRuntimeGraphs(std::span<camel::runtime::GCGraph *const> graphs) {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->trackDetached(graphs);
}

void Context::releaseDetachedRuntimeGraphs() {
    ASSERT(runtimeGraphMgr_ != nullptr, "Runtime graph manager is not initialized.");
    runtimeGraphMgr_->releaseDetached();
}

void Context::registerExecutorFactory(std::string name, executor_factory_t fact) {
    exeMgr_->registerExecutorFactory(name, fact);
}

void Context::registerRuntimeGraphDebugInfo(camel::runtime::GCGraph *runtimeRoot) {
    if (!runtimeRoot || !sourceContext_) {
        return;
    }
    auto &debugMap = sourceContext_->debugMap();
    camel::runtime::forEachReachableGraph(runtimeRoot, [&](camel::runtime::GCGraph *graph) {
        if (!graph) {
            return;
        }
        camel::source::origin_id_t origin = debugMap.graphOrigin(graph->stableId());
        if (origin != camel::source::kInvalidOriginId) {
            debugMap.registerRuntimeGraphOrigin(reinterpret_cast<uintptr_t>(graph), origin);
        }
    });
}

} // namespace camel::core::context
