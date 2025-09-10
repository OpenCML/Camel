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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <filesystem>

#include "common/module/userdef.h"
#include "context.h"
#include "module/userdef.h"
#include "utils/log.h"
#include "utils/str.h"

namespace fs = std::filesystem;

inline bool fileExists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

context_ptr_t Context::create(const EntryConfig &entryConf, const DiagnosticsConfig &diagConf) {
    context_ptr_t ctx = std::shared_ptr<Context>(new Context(entryConf, diagConf));
    ctx->exeMgr_ = std::make_unique<ExecutorManager>(ctx);
    if (auto builtin = getBuiltinModule("", ctx); builtin.has_value()) {
        l.in("Context").info("Context initialized using entry config {}", entryConf.toString());
        ctx->modules_[""] = builtin.value();
    }
    return ctx;
}

module_ptr_t
Context::importModule(const std::string &rawModuleName, const std::string &currentModuleName) {
    if (rawModuleName.empty()) {
        return modules_[""]; // builtin module already loaded
    }

    l.in("Context").info(
        "Importing module '{}' from current module '{}'.",
        rawModuleName,
        currentModuleName);
    auto candidates = getModuleNameCandidates(currentModuleName, rawModuleName);

    for (const auto &name : candidates) {
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            l.in("Context").debug("Module '{}' found in cache.", name);
            return it->second;
        }

        module_ptr_t module = tryLoadModule(name);
        if (module) {
            l.in("Context").debug("Module '{}' loaded from file '{}'.", name, module->path());
            modules_[name] = module;
            return module;
        }

        auto builtin = getBuiltinModule(name, shared_from_this());
        if (builtin.has_value()) {
            l.in("Context").debug("Module '{}' found in built-in modules.", name);
            modules_[name] = builtin.value();
            return builtin.value();
        }
    }

    throw CamelBaseException("Module not found '" + rawModuleName);
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
        std::vector<std::string> base = split(currentModule, '.');
        for (int i = base.size(); i >= 0; --i) {
            std::vector<std::string> prefix(base.begin(), base.begin() + i);
            prefix.push_back(rawImportName);
            candidates.push_back(join(prefix, '.'));
        }
    }

    return candidates;
}

std::string Context::resolveRelativeModuleName(
    const std::string &currentModule, const std::string &importName) {

    int level = 0;
    size_t i = 0;
    while (i < importName.size() && importName[i] == '.') {
        ++level;
        ++i;
    }

    std::string remaining = importName.substr(i);
    std::vector<std::string> base = split(currentModule, '.');

    if (static_cast<size_t>(level) > base.size()) {
        throw CamelBaseException("Too many dots in relative import: " + importName);
    }

    base.resize(base.size() - level);

    if (!remaining.empty()) {
        std::vector<std::string> rest = split(remaining, '.');
        base.insert(base.end(), rest.begin(), rest.end());
    }

    return join(base, '.');
}

std::string Context::getModulePath(const std::string &moduleName) {
    std::string relativePath = moduleName;
    std::replace(relativePath.begin(), relativePath.end(), '.', '/');
    relativePath += ".cml";

    for (const auto &dir : entryConfig_.searchPaths) {
        if (dir.empty())
            continue; // skip empty search paths
        fs::path basePath = fs::path(dir);
        if (!basePath.is_absolute()) {
            basePath = fs::path(entryConfig_.entryDir) / basePath;
        }

        fs::path fullPath = basePath / relativePath;
        if (fileExists(fullPath.string())) {
            return fullPath.string();
        }
    }

    // fallback: root/relativePath
    fs::path fallbackPath = fs::path(entryConfig_.entryDir) / relativePath;
    if (fileExists(fallbackPath.string())) {
        return fallbackPath.string();
    }

    return "";
}

bool Context::moduleFileExists(const std::string &moduleName) {
    return !getModulePath(moduleName).empty();
}

module_ptr_t Context::tryLoadModule(const std::string &moduleName) {
    std::string path = getModulePath(moduleName);
    if (!path.empty()) {
        return UserDefinedModule::fromFile(moduleName, path, shared_from_this());
    }
    return nullptr;
}

GIR::graph_ptr_t Context::rootGraph() const {
    ASSERT(mainModule_ != nullptr, "Main module is not set in context.");
    auto gir = tt::as_shared<UserDefinedModule>(mainModule_)->gir();
    ASSERT(gir != nullptr, "GIR of main module is not built yet.");
    return gir;
}

GIR::graph_ptr_t Context::mainGraph() const {
    ASSERT(mainModule_ != nullptr, "Main module is not set in context.");
    auto gir = tt::as_shared<UserDefinedModule>(mainModule_)->gir();
    ASSERT(gir != nullptr, "GIR of main module is not built yet.");
    const auto optMainGraph = gir->getSubGraph("main");
    if (!optMainGraph.has_value()) {
        throw CamelBaseException("Main graph not found in GIR of main module.");
    }
    return optMainGraph.value();
}