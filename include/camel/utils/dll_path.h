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
 * Created: Feb. 23, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/utils/install_layout.h"

#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <cstdlib>
#include <limits.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <cstdlib>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

namespace camel {
namespace utils {

/** Return the current process executable directory; return an empty path on failure. */
inline std::filesystem::path getExecutableDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        return {};
    std::filesystem::path p(path);
    return p.has_filename() ? p.parent_path() : p;
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return {};
    buf[len] = '\0';
    std::filesystem::path p(buf);
    return p.has_filename() ? p.parent_path() : p;
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return {};
    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == nullptr)
        return {};
    std::filesystem::path p(resolved);
    return p.has_filename() ? p.parent_path() : p;
#else
    return {};
#endif
}

inline void addLibrarySearchPath(const std::filesystem::path &dir) {
    namespace fs = std::filesystem;
    if (dir.empty()) {
        return;
    }
    fs::path abs = fs::absolute(dir).lexically_normal();
#ifdef _WIN32
    if (SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        std::wstring ws = abs.wstring();
        if (!ws.empty() && ws.back() != L'/' && ws.back() != L'\\') {
            ws += L'\\';
        }
        AddDllDirectory(ws.c_str());
    }
#elif defined(__linux__)
    std::string key = "LD_LIBRARY_PATH";
#elif defined(__APPLE__)
    std::string key = "DYLD_LIBRARY_PATH";
#endif
#if defined(__linux__) || defined(__APPLE__)
    std::string append   = abs.string();
    const char *existing = std::getenv(key.c_str());
    if (existing && existing[0]) {
        append += ":";
        append += existing;
    }
    setenv(key.c_str(), append.c_str(), 1);
#endif
}

/// SDK tools only: derive library search paths from the unified InstallRoot.
inline void setupLibrarySearchPathForHost() {
#ifdef _WIN32
    if (!SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        auto dirs = buildHostLibrarySearchDirs();
        if (dirs.size() > 1) {
            SetDllDirectoryW(dirs[1].wstring().c_str());
        }
        return;
    }
#endif
    for (const auto &dir : buildHostLibrarySearchDirs()) {
        addLibrarySearchPath(dir);
    }
}

/// App-side setup: use CAMEL_HOME as the base directory for library search paths.
/// This is intended for camel-cpp-built executables that may be deployed
/// anywhere and locate the SDK via the CAMEL_HOME environment variable.
/// CAMEL_HOME should point to the SDK root (for example, out/latest), which
/// should contain bin/ and libs/ subdirectories.
/// If CAMEL_HOME is not set, fall back to current_path().
/// Search order: executable directory first, then CAMEL_HOME/libs,
/// CAMEL_HOME/bin, and CAMEL_HOME itself.
inline void setupLibrarySearchPathForApp() {
#ifdef _WIN32
    if (!SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        auto dirs = buildAppLibrarySearchDirs();
        if (dirs.size() > 1) {
            SetDllDirectoryW(dirs[1].wstring().c_str());
        }
        return;
    }
#endif
    for (const auto &dir : buildAppLibrarySearchDirs()) {
        addLibrarySearchPath(dir);
    }
}

} // namespace utils
} // namespace camel
