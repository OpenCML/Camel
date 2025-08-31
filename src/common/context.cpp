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
#include "utils/str.h"

#include <filesystem>

namespace fs = std::filesystem;

inline bool fileExists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

Context::Context(const EntryConfig &entryConf, const DiagnosticsConfig &diagConf)
    : entryConfig_(entryConf), diagConfig_(diagConf) {
    if (auto builtin = getBuiltinModule(""); builtin.has_value()) {
        modules_[""] = builtin.value();
    }
}

module_ptr_t
Context::importModule(const std::string &rawModuleName, const std::string &currentModuleName) {

    // 1. 解析模块名
    auto resolvedOpt = resolveModuleName(currentModuleName, rawModuleName);
    if (!resolvedOpt.has_value()) {
        throw std::runtime_error("Cannot resolve module: " + rawModuleName);
    }

    const std::string &resolvedName = resolvedOpt.value();

    // 2. 缓存查找
    auto it = modules_.find(resolvedName);
    if (it != modules_.end()) {
        return it->second;
    }

    // 3. 尝试加载用户模块
    module_ptr_t module = tryLoadModule(resolvedName);
    if (module) {
        modules_[resolvedName] = module;
        return module;
    }

    // 4. 尝试加载内建模块
    auto builtin = getBuiltinModule(resolvedName);
    if (builtin.has_value()) {
        modules_[resolvedName] = builtin.value();
        return builtin.value();
    }

    throw std::runtime_error("Module not found: " + rawModuleName);
}

std::optional<std::string>
Context::resolveModuleName(const std::string &currentModule, const std::string &rawImportName) {

    std::vector<std::string> candidates;

    if (rawImportName.empty())
        return std::nullopt;

    if (rawImportName[0] == '.') {
        // 相对导入
        try {
            std::string resolved = resolveRelativeModuleName(currentModule, rawImportName);
            candidates.push_back(resolved);
        } catch (...) {
            return std::nullopt;
        }
    } else {
        // 非相对导入，尝试多种可能
        candidates.push_back(rawImportName); // 顶层模块

        std::vector<std::string> base = split(currentModule, '.');
        for (int i = base.size(); i >= 0; --i) {
            std::vector<std::string> prefix(base.begin(), base.begin() + i);
            prefix.push_back(rawImportName);
            candidates.push_back(join(prefix, '.'));
        }
    }

    // 遍历候选模块名，返回第一个存在的
    for (const auto &name : candidates) {
        if (moduleFileExists(name)) {
            return name;
        }
    }

    return std::nullopt;
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
    relativePath += ".cml"; // 假设模块文件后缀是 .cml

    // 1. 遍历 searchPaths
    for (const auto &dir : entryConfig_.searchPaths) {
        fs::path fullPath = fs::path(entryConfig_.root) / dir / relativePath;
        if (fileExists(fullPath.string())) {
            return fullPath.string();
        }
    }

    // 2. fallback: root + relativePath
    fs::path fallbackPath = fs::path(entryConfig_.root) / relativePath;
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