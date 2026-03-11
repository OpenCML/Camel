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
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/utils/env.h"

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

/// SDK 工具专用：以 exe 所在目录为 base 设置库搜索路径，并 fallback 到 CAMEL_HOME。
/// 适用于 camel、camel-cpp、format 等运行在 SDK 内的可执行文件，旁边通常有 libs。
/// 搜索顺序：exe 所在目录、exe/libs、exe/../libs；若设置 CAMEL_HOME 则追加
/// CAMEL_HOME/libs、CAMEL_HOME/bin、CAMEL_HOME。
inline void setupLibrarySearchPathForHost() {
    namespace fs        = std::filesystem;
    std::string sdkRoot = getEnv("CAMEL_HOME", fs::current_path().string());
    fs::path camelHome(sdkRoot);

#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        return;

    std::wstring exePath(path);
    size_t last = exePath.find_last_of(L"\\/");
    if (last == std::wstring::npos)
        return;

    std::wstring base    = exePath.substr(0, last + 1);
    std::wstring libsDir = base + L"libs";
    std::wstring parentLibs;
    size_t sep = base.find_last_of(L"\\/", base.length() > 1 ? base.length() - 2 : 0);
    if (sep != std::wstring::npos)
        parentLibs = base.substr(0, sep + 1) + L"libs";

    if (SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        AddDllDirectory(base.c_str());
        AddDllDirectory(libsDir.c_str());
        if (!parentLibs.empty())
            AddDllDirectory(parentLibs.c_str());
        if (!sdkRoot.empty()) {
            std::wstring ch = camelHome.wstring();
            if (!ch.empty() && ch.back() != L'/' && ch.back() != L'\\')
                ch += L'\\';
            AddDllDirectory(ch.c_str());
            AddDllDirectory((ch + L"libs").c_str());
            AddDllDirectory((ch + L"bin").c_str());
        }
    } else {
        SetDllDirectoryW(libsDir.c_str());
    }

#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return;
    buf[len] = '\0';

    std::string exePath(buf);
    size_t last = exePath.find_last_of("/");
    if (last == std::string::npos)
        return;

    std::string base      = exePath.substr(0, last + 1);
    std::string localLibs = base + "libs";
    std::string parentLibs;
    size_t sep = base.rfind('/', base.length() > 1 ? base.length() - 2 : 0);
    if (sep != std::string::npos)
        parentLibs = base.substr(0, sep + 1) + "libs";

    std::string newPath = base + ":" + localLibs;
    if (!parentLibs.empty())
        newPath += ":" + parentLibs;
    if (!sdkRoot.empty()) {
        std::string ch = camelHome.string();
        if (!ch.empty() && ch.back() != '/')
            ch += '/';
        newPath += ":" + ch + ":" + ch + "libs:" + ch + "bin";
    }
    const char *existing = std::getenv("LD_LIBRARY_PATH");
    if (existing && existing[0])
        newPath += ":";
    if (existing)
        newPath += existing;

    setenv("LD_LIBRARY_PATH", newPath.c_str(), 1);

#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return;

    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == nullptr)
        return;

    std::string exePath(resolved);
    size_t last = exePath.find_last_of("/");
    if (last == std::string::npos)
        return;

    std::string base      = exePath.substr(0, last + 1);
    std::string localLibs = base + "libs";
    std::string parentLibs;
    size_t sep = base.rfind('/', base.length() > 1 ? base.length() - 2 : 0);
    if (sep != std::string::npos)
        parentLibs = base.substr(0, sep + 1) + "libs";

    std::string newPath = base + ":" + localLibs;
    if (!parentLibs.empty())
        newPath += ":" + parentLibs;
    if (!sdkRoot.empty()) {
        std::string ch = camelHome.string();
        if (!ch.empty() && ch.back() != '/')
            ch += '/';
        newPath += ":" + ch + ":" + ch + "libs:" + ch + "bin";
    }
    const char *existing = std::getenv("DYLD_LIBRARY_PATH");
    if (existing && existing[0])
        newPath += ":";
    if (existing)
        newPath += existing;

    setenv("DYLD_LIBRARY_PATH", newPath.c_str(), 1);

#else
    (void)camelHome;
#endif
}

/// 导出应用专用：以 CAMEL_HOME 为 base 设置库搜索路径。
/// 适用于 camel-cpp 编译出的 exe，可能部署在任意位置，通过环境变量 CAMEL_HOME 定位 SDK 目录。
/// CAMEL_HOME 指向 SDK 根目录（如 out/latest），其下应有 bin/、libs/ 等子目录。
/// 若未设置 CAMEL_HOME，则 fallback 为 current_path()。
/// 搜索顺序：exe 所在目录（优先）、CAMEL_HOME/libs、CAMEL_HOME/bin、CAMEL_HOME。
inline void setupLibrarySearchPathForApp() {
    namespace fs        = std::filesystem;
    auto exeDir         = getExecutableDirectory();
    std::string sdkRoot = getEnv("CAMEL_HOME", fs::current_path().string());
    if (sdkRoot.empty())
        return;
    fs::path root(sdkRoot);
#ifdef _WIN32
    std::wstring exeBase;
    if (!exeDir.empty()) {
        exeBase = exeDir.wstring();
        if (!exeBase.empty() && exeBase.back() != L'/' && exeBase.back() != L'\\')
            exeBase += L'\\';
    }
    std::wstring base(root.wstring());
    if (base.empty() || (base.back() != L'/' && base.back() != L'\\'))
        base += L'\\';
    std::wstring libs = base + L"libs";
    std::wstring bin  = base + L"bin";
    if (SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        if (!exeBase.empty())
            AddDllDirectory(exeBase.c_str());
        AddDllDirectory(base.c_str());
        AddDllDirectory(libs.c_str());
        AddDllDirectory(bin.c_str());
    } else {
        SetDllDirectoryW(libs.c_str());
    }
#elif defined(__linux__)
    std::string exeBase =
        exeDir.empty() ? ""
                       : (exeDir.string() +
                          (exeDir.string().empty() || exeDir.string().back() == '/' ? "" : "/"));
    std::string base = root.string();
    if (!base.empty() && base.back() != '/')
        base += '/';
    std::string libs     = base + "libs";
    std::string bin      = base + "bin";
    const char *existing = std::getenv("LD_LIBRARY_PATH");
    std::string newPath  = exeBase.empty() ? "" : (exeBase + ":");
    newPath += base + ":" + libs + ":" + bin;
    if (existing && existing[0])
        newPath += ":";
    if (existing)
        newPath += existing;
    setenv("LD_LIBRARY_PATH", newPath.c_str(), 1);
#elif defined(__APPLE__)
    std::string exeBase =
        exeDir.empty() ? ""
                       : (exeDir.string() +
                          (exeDir.string().empty() || exeDir.string().back() == '/' ? "" : "/"));
    std::string base = root.string();
    if (!base.empty() && base.back() != '/')
        base += '/';
    std::string libs     = base + "libs";
    std::string bin      = base + "bin";
    const char *existing = std::getenv("DYLD_LIBRARY_PATH");
    std::string newPath  = exeBase.empty() ? "" : (exeBase + ":");
    newPath += base + ":" + libs + ":" + bin;
    if (existing && existing[0])
        newPath += ":";
    if (existing)
        newPath += existing;
    setenv("DYLD_LIBRARY_PATH", newPath.c_str(), 1);
#else
    (void)root;
#endif
}

} // namespace utils
} // namespace camel
