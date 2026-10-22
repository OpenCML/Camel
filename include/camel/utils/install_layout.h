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
 * Created: Apr. 01, 2026
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/utils/env.h"
#include "camel/utils/path.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

namespace camel::utils {

namespace detail {

inline std::filesystem::path executableDirectoryRaw() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return {};
    }
    std::filesystem::path p(path);
    return p.has_filename() ? p.parent_path() : p;
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return {};
    }
    buf[len] = '\0';
    std::filesystem::path p(buf);
    return p.has_filename() ? p.parent_path() : p;
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return {};
    }
    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == nullptr) {
        return {};
    }
    std::filesystem::path p(resolved);
    return p.has_filename() ? p.parent_path() : p;
#else
    return {};
#endif
}

inline std::vector<std::string> splitPathList(const std::string &value) {
    std::vector<std::string> out;
    if (value.empty()) {
        return out;
    }
#ifdef _WIN32
    constexpr char sep = ';';
#else
    constexpr char sep = ':';
#endif
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(sep, start);
        std::string token =
            (end == std::string::npos) ? value.substr(start) : value.substr(start, end - start);
        if (!token.empty()) {
            out.push_back(token);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

inline void pushUniquePath(
    std::vector<std::string> &out, std::unordered_set<std::string> &seen,
    const std::filesystem::path &candidate, const std::filesystem::path &baseDir = {}) {
    if (candidate.empty()) {
        return;
    }
    std::filesystem::path p = candidate;
    if (!p.is_absolute()) {
        if (!baseDir.empty()) {
            p = baseDir / p;
        } else {
            p = std::filesystem::current_path() / p;
        }
    }
    p               = std::filesystem::absolute(p).lexically_normal();
    std::string key = pathToUtf8(p);
    if (seen.insert(key).second) {
        out.push_back(key);
    }
}

} // namespace detail

struct ModuleSearchPathOptions {
    std::string stdlibOverride;
    /// Extra roots from `-I` / `--include`, in command-line order. They come first among the
    /// module roots and precede every environment-provided root (design principles 10.2: a
    /// module's own directory, then the `-I` roots in the order given, then the standard
    /// library; `CAMEL_PACKAGES` adds roots of the same kind as `-I`, after them).
    std::vector<std::string> includeDirs;
    bool includeInstallRoot       = true;
    bool includeExecutableRelated = true;
    bool includeCamelPackages     = true;
    bool includeCamelModulePath   = true;
};

inline std::filesystem::path resolveInstallRoot() {
    namespace fs        = std::filesystem;
    std::string envRoot = getEnv("CAMEL_HOME");
    if (!envRoot.empty()) {
        return fs::absolute(pathFromUtf8(envRoot)).lexically_normal();
    }

    fs::path exeDir = detail::executableDirectoryRaw();
    if (!exeDir.empty()) {
        fs::path norm = fs::absolute(exeDir).lexically_normal();
        if (norm.filename() == "bin" && norm.has_parent_path()) {
            return norm.parent_path();
        }
        return norm;
    }
    return fs::absolute(fs::current_path()).lexically_normal();
}

inline std::vector<std::string> buildModuleSearchPaths(
    const std::string &entryDir, const ModuleSearchPathOptions &opts = ModuleSearchPathOptions{}) {
    namespace fs = std::filesystem;

    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    fs::path baseEntry = entryDir.empty() ? fs::current_path() : pathFromUtf8(entryDir);
    baseEntry          = fs::absolute(baseEntry).lexically_normal();

    // The entry file's directory is *not* one of the roots: it is the entry program's own
    // directory, and a module's own directory comes before these roots (design principles
    // 10.2). Baking it in here made it a root for every library too, so a library's import
    // resolved differently depending on who imported it (H220). The context puts the importing
    // module's directory in front of this list.
    //
    // `-I` / `--include`: the module roots, in command-line order, before every root that comes
    // from the environment. A relative root is resolved against the entry directory, like the
    // roots of CAMEL_PACKAGES below (the whole module root list has one base).
    for (const auto &item : opts.includeDirs) {
        detail::pushUniquePath(paths, seen, pathFromUtf8(item), baseEntry);
    }

    const auto appendEnvList = [&](const std::string &name) {
        for (const auto &item : detail::splitPathList(getEnv(name))) {
            detail::pushUniquePath(paths, seen, pathFromUtf8(item), baseEntry);
        }
    };

    if (opts.includeCamelPackages) {
        appendEnvList("CAMEL_PACKAGES");
    }
    if (opts.includeCamelModulePath) {
        appendEnvList("CAMEL_MODULE_PATH");
    }

    std::string stdlib =
        opts.stdlibOverride.empty() ? getEnv("CAMEL_STD_LIB") : opts.stdlibOverride;
    if (!stdlib.empty()) {
        detail::pushUniquePath(paths, seen, pathFromUtf8(stdlib), baseEntry);
    }

    fs::path exeDir = detail::executableDirectoryRaw();
    if (opts.includeExecutableRelated) {
        if (exeDir.empty()) {
            exeDir = fs::current_path();
        }
        detail::pushUniquePath(paths, seen, exeDir);
        detail::pushUniquePath(paths, seen, exeDir / "stdlib");
        detail::pushUniquePath(paths, seen, exeDir.parent_path() / "stdlib");
    }

    if (opts.includeInstallRoot) {
        fs::path installRoot = resolveInstallRoot();
        detail::pushUniquePath(paths, seen, installRoot);
        detail::pushUniquePath(paths, seen, installRoot / "stdlib");
        detail::pushUniquePath(paths, seen, installRoot / "bin");
        detail::pushUniquePath(paths, seen, installRoot / "bin" / "stdlib");
    }
    return paths;
}

inline std::vector<std::filesystem::path> buildHostLibrarySearchDirs() {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    fs::path exeDir = detail::executableDirectoryRaw();
    if (exeDir.empty()) {
        exeDir = fs::current_path();
    }
    dirs.push_back(fs::absolute(exeDir).lexically_normal());
    dirs.push_back(fs::absolute(exeDir / "libs").lexically_normal());
    dirs.push_back(fs::absolute(exeDir.parent_path() / "libs").lexically_normal());

    fs::path installRoot = resolveInstallRoot();
    dirs.push_back(installRoot);
    dirs.push_back((installRoot / "libs").lexically_normal());
    dirs.push_back((installRoot / "bin").lexically_normal());
    return dirs;
}

inline std::vector<std::filesystem::path> buildAppLibrarySearchDirs() {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    fs::path exeDir = detail::executableDirectoryRaw();
    if (!exeDir.empty()) {
        dirs.push_back(fs::absolute(exeDir).lexically_normal());
    }
    fs::path installRoot = resolveInstallRoot();
    dirs.push_back(installRoot);
    dirs.push_back((installRoot / "libs").lexically_normal());
    dirs.push_back((installRoot / "bin").lexically_normal());
    return dirs;
}

} // namespace camel::utils
