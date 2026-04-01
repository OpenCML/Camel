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
 * Updated: Apr. 01, 2026
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

/** 返回当前进程可执行文件所在目录；失败时返回空 path。 */
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

/// SDK 工具专用：使用统一 InstallRoot 推导库搜索路径。
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

/// 导出应用专用：以 CAMEL_HOME 为 base 设置库搜索路径。
/// 适用于 camel-cpp 编译出的 exe，可能部署在任意位置，通过环境变量 CAMEL_HOME 定位 SDK 目录。
/// CAMEL_HOME 指向 SDK 根目录（如 out/latest），其下应有 bin/、libs/ 等子目录。
/// 若未设置 CAMEL_HOME，则 fallback 为 current_path()。
/// 搜索顺序：exe 所在目录（优先）、CAMEL_HOME/libs、CAMEL_HOME/bin、CAMEL_HOME。
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
