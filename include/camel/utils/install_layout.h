// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/utils/env.h"
#include "camel/utils/path.h"

#include <filesystem>
#include <stdexcept>
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
};

/// What is wrong with `CAMEL_HOME`, or an empty string when the variable is unset or names a
/// usable installation root.
///
/// The variable is the one way a user says which installation to run, and it is a switch of the
/// environment that *is* a switch of behaviour (design principles: a switch becomes an option, and
/// an illegal value is an error). It used to be taken as given: a path that does not exist, or a
/// directory that is not an installation, silently produced a run whose module roots point nowhere,
/// and every import failed with "not found" long after anything mentioned `CAMEL_HOME`. The
/// directory must exist and hold at least one of the directories an installation has (`bin`,
/// `stdlib`, `libs`, `include`); the message names the variable and the path, so the run that
/// refuses says what to fix.
inline std::string camelHomeProblem() {
    namespace fs        = std::filesystem;
    std::string envRoot = getEnv("CAMEL_HOME");
    if (envRoot.empty()) {
        return {};
    }
    const fs::path dir = fs::absolute(pathFromUtf8(envRoot)).lexically_normal();
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return "CAMEL_HOME points to '" + pathToUtf8(dir) + "', which does not exist";
    }
    if (!fs::is_directory(dir, ec) || ec) {
        return "CAMEL_HOME points to '" + pathToUtf8(dir) + "', which is not a directory";
    }
    for (const char *part : {"bin", "stdlib", "libs", "include"}) {
        if (fs::is_directory(dir / part, ec) && !ec) {
            return {};
        }
    }
    return "CAMEL_HOME points to '" + pathToUtf8(dir) +
           "', which holds none of the directories of an installation (bin, stdlib, libs, "
           "include)";
}

/// The installation root a run reads its standard library and its runtime from: `CAMEL_HOME` when
/// it is set (and valid: see camelHomeProblem), else the directory above the running executable
/// when that is an installation's `bin`.
///
/// Neither fallback is the current directory any more: when the executable's directory cannot be
/// read there is nothing to derive an installation from, and a run in whatever directory the shell
/// happened to be in would silently take that directory for the installation root. It throws
/// instead, and the caller reports the message.
inline std::filesystem::path resolveInstallRoot() {
    namespace fs = std::filesystem;
    if (const std::string problem = camelHomeProblem(); !problem.empty()) {
        throw std::runtime_error(problem);
    }
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
    throw std::runtime_error(
        "the directory of the running executable cannot be read, so the installation root cannot "
        "be found; set CAMEL_HOME to the installation root");
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
    // resolved differently depending on who imported it. The context puts the importing
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

    std::string stdlib =
        opts.stdlibOverride.empty() ? getEnv("CAMEL_STD_LIB") : opts.stdlibOverride;
    if (!stdlib.empty()) {
        detail::pushUniquePath(paths, seen, pathFromUtf8(stdlib), baseEntry);
    }

    fs::path exeDir = detail::executableDirectoryRaw();
    if (opts.includeExecutableRelated) {
        if (exeDir.empty()) {
            // The roots next to the running executable are where an installation is found; without
            // the executable's directory they cannot be determined, and guessing the current
            // directory would make the roots depend on where the shell stood.
            throw std::runtime_error(
                "the directory of the running executable cannot be read, so the module roots next "
                "to it cannot be determined; set CAMEL_HOME to the installation root");
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
