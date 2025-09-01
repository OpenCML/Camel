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

#include "context.h"
#include "common/module/userdef.h"
#include "utils/log.h"
#include "utils/str.h"

#define DEBUG_LEVEL -1

#include <filesystem>

namespace fs = std::filesystem;

std::ostream &operator<<(std::ostream &os, const EntryConfig &config) {
    os << "{\n";
    os << "  entryDir: " << config.entryDir << "\n";
    os << "  entryFile: " << config.entryFile << "\n";
    os << "  searchPaths: [";
    for (size_t i = 0; i < config.searchPaths.size(); ++i) {
        if (config.searchPaths[i].empty())
            continue;
        os << config.searchPaths[i];
        if (i < config.searchPaths.size() - 1)
            os << ", ";
    }
    os << "]\n";
    os << "  envs: {\n";
    for (const auto &[key, value] : config.envs) {
        os << "    " << key << ": " << value << "\n";
    }
    os << "  }\n";
    os << "}";
    return os;
}

inline bool fileExists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

Context::Context(const EntryConfig &entryConf, const DiagnosticsConfig &diagConf)
    : entryConfig_(entryConf), diagConfig_(diagConf) {
    if (auto builtin = getBuiltinModule(""); builtin.has_value()) {
        debug(0) << "EntryConfig:\n" << entryConf << std::endl;
        modules_[""] = builtin.value();
    }
}

module_ptr_t
Context::importModule(const std::string &rawModuleName, const std::string &currentModuleName) {
    auto candidates = getModuleNameCandidates(currentModuleName, rawModuleName);

    for (const auto &name : candidates) {
        std::cout << "Trying to import module: " << name << std::endl;
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            std::cout << "Module found in cache: " << name << std::endl;
            return it->second;
        }

        module_ptr_t module = tryLoadModule(name);
        if (module) {
            std::cout << "Module loaded: " << name << " from " << module->path() << std::endl;
            modules_[name] = module;
            return module;
        }

        auto builtin = getBuiltinModule(name);
        if (builtin.has_value()) {
            std::cout << "Builtin module loaded: " << name << std::endl;
            modules_[name] = builtin.value();
            return builtin.value();
        }
    }

    throw std::runtime_error("Module not found: " + rawModuleName);
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
        throw std::runtime_error("Too many dots in relative import: " + importName);
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
        return UserDefinedModule::loadFromFile(moduleName, path, shared_from_this());
    }
    return nullptr;
}