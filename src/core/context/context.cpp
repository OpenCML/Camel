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
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <filesystem>

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
    EXEC_WHEN_DEBUG(l.in("Context").debug("Looking for built-in module: <{}>", name));
    auto it = builtinModules_.find(name);
    if (it != builtinModules_.end()) {
        return it->second;
    }

    auto factoryIt = builtinModuleFactories.find(name);
    if (factoryIt != builtinModuleFactories.end()) {
        EXEC_WHEN_DEBUG(l.in("Context").debug("Loading built-in module: <{}>", name));
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
        l.in("Context").info("Context initialized using entry config {}", entryConf.toString()));
    return ctx;
}

module_ptr_t
Context::importModule(const std::string &rawModuleName, const std::string &currentModuleName) {
    if (rawModuleName.empty()) {
        return modules_[""]; // builtin module already loaded
    }

    EXEC_WHEN_DEBUG(l.in("Context").info(
        "Importing module '{}' from current module '{}'.",
        rawModuleName,
        currentModuleName));
    auto candidates = getModuleNameCandidates(currentModuleName, rawModuleName);

    for (const auto &name : candidates) {
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            EXEC_WHEN_DEBUG(l.in("Context").debug("Module '{}' found in cache.", name));
            return it->second;
        }

        module_ptr_t module = tryLoadModule(name);
        if (module) {
            EXEC_WHEN_DEBUG(
                l.in("Context").debug("Module '{}' loaded from file '{}'.", name, module->path()));
            modules_[name] = module;
            return module;
        }

        auto builtin = getBuiltinModule(name);
        if (builtin.has_value()) {
            EXEC_WHEN_DEBUG(l.in("Context").debug("Module '{}' found in built-in modules.", name));
            modules_[name] = builtin.value();
            return builtin.value();
        }
    }

    throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound).commit(rawModuleName);
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

    auto tryDir = [&](const fs::path &base) -> std::pair<std::string, bool> {
        fs::path dir = base / parentDir;
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
    if (isCmo)
        return loadCmoModule(moduleName, path, shared_from_this());
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

void Context::eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame) {
    return exeMgr_->eval(uri, self, frame);
}
