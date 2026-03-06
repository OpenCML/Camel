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
 * Updated: Mar. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <filesystem>
#include <unordered_set>

#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/module/dynamic.h"
#include "camel/core/module/userdef.h"
#include "camel/utils/log.h"
#include "camel/utils/str.h"

namespace fs = std::filesystem;
using namespace strutil;

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
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("Context").debug("Looking for built-in module: <{}>", name));
    auto it = builtinModules_.find(name);
    if (it != builtinModules_.end()) {
        return it->second;
    }

    auto factoryIt = builtinModuleFactories.find(name);
    if (factoryIt != builtinModuleFactories.end()) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("Context").debug("Loading built-in module: <{}>", name));
        module_ptr_t module = factoryIt->second(shared_from_this());
        module->load(); // instantly load the builtin module
        builtinModules_[name] = module;
        return module;
    }

    return std::nullopt;
}

context_ptr_t Context::create(const EntryConfig &entryConf, const DiagsConfig &diagConf) {
    context_ptr_t ctx = std::shared_ptr<Context>(new Context(entryConf, diagConf));
    ctx->exeMgr_      = std::make_unique<ExecutorManager>(ctx);
    ctx->rtmDiags_    = std::make_shared<Diagnostics>("main", entryConf.entryFile);
    ctx->rtmDiags_->setConfig(diagConf);
    ctx->modules_[""] = ctx->getBuiltinModule("").value();
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("Context").info(
            "Context initialized using entry config {}",
            entryConf.toString()));
    return ctx;
}

void Context::dumpAllModuleDiagnostics(std::ostream &os, bool json) const {
    auto allMods = allUserModules();
    std::vector<module_ptr_t> modsErr;
    for (const auto &mod : allMods) {
        auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
        if (ud && ud->diagnostics() && ud->diagnostics()->hasErrors()) {
            modsErr.push_back(mod);
        }
    }
    if (json && !modsErr.empty()) {
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
        }
    }
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

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("Context").info(
            "Importing module '{}' from current module '{}'.",
            rawModuleName,
            currentModuleName));
    lastCmoLoadError_.clear();
    auto candidates = getModuleNameCandidates(currentModuleName, rawModuleName);

    for (const auto &name : candidates) {
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("Context").debug("Module '{}' found in cache.", name));
            return it->second;
        }

        module_ptr_t module = tryLoadModule(name);
        if (module) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("Context").debug(
                    "Module '{}' loaded from file '{}'.",
                    name,
                    module->path()));
            if (!module->loaded()) {
                module->load();
            }
            modules_[name] = module;
            return module;
        }

        auto builtin = getBuiltinModule(name);
        if (builtin.has_value()) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("Context").debug(
                    "Module '{}' found in built-in modules.",
                    name));
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

    /// 在给定目录 D 中的查找顺序：1) D/xx.cmo  2) D/xx.*（任意同名文本/源文件）
    /// 若 D/xx 为目录，则再查：3) D/xx/xx.cmo  4) D/xx/xx.*
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
    auto [path, isCmo] = getModulePathAndKind(moduleName);
    if (path.empty())
        return nullptr;
    if (isCmo) {
        std::string err;
        module_ptr_t mod = loadCmoModule(moduleName, path, shared_from_this(), &err);
        if (!mod && !err.empty())
            lastCmoLoadError_ = std::move(err);
        return mod;
    }
    return UserDefinedModule::fromFile(moduleName, path, shared_from_this());
}

GraphIR::graph_ptr_t Context::rootGraph() const {
    ASSERT(mainModule_ != nullptr, "Main module is not set in context.");
    auto gir = tt::as_shared<UserDefinedModule>(mainModule_)->gir();
    ASSERT(gir != nullptr, "GraphIR of main module is not built yet.");
    return gir;
}

GraphIR::graph_ptr_t Context::mainGraph() const {
    ASSERT(mainModule_ != nullptr, "Main module is not set in context.");
    auto gir = tt::as_shared<UserDefinedModule>(mainModule_)->gir();
    ASSERT(gir != nullptr, "GraphIR of main module is not built yet.");
    const auto optMainGraphSet = gir->getSubGraphsByName("main");
    if (!optMainGraphSet.has_value()) {
        throw DiagnosticBuilder::of(RuntimeDiag::RuntimeError)
            .commit("Main graph not found in GraphIR of main module.");
    }
    if (optMainGraphSet->empty()) {
        throw DiagnosticBuilder::of(RuntimeDiag::RuntimeError)
            .commit("Main graph set is empty in GraphIR of main module.");
    }
    return *optMainGraphSet.value().begin();
}

void Context::registerExecutorFactory(std::string name, executor_factory_t fact) {
    exeMgr_->registerExecutorFactory(name, fact);
}

void Context::eval(std::string uri, GraphIR::Node *self, Frame &frame) {
    return exeMgr_->eval(uri, self, frame);
}
