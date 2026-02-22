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
 * Updated: Feb. 23, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

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

// 设置库搜索路径，以 exe 所在目录为 base：
// 1) base (./)  2) base/libs (./libs)  3) base/../libs (../libs)  4) 系统默认
inline void setupLibrarySearchPath() {
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

    // 使用 AddDllDirectory 添加 base, base/libs, base/../libs
    if (SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        AddDllDirectory(base.c_str());
        AddDllDirectory(libsDir.c_str());
        if (!parentLibs.empty())
            AddDllDirectory(parentLibs.c_str());
    } else {
        // 降级：SetDllDirectory 只能设一个，优先 base/libs
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

    const char *existing = std::getenv("LD_LIBRARY_PATH");
    std::string newPath  = base + ":" + localLibs;
    if (!parentLibs.empty())
        newPath += ":" + parentLibs;
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

    const char *existing = std::getenv("DYLD_LIBRARY_PATH");
    std::string newPath  = base + ":" + localLibs;
    if (!parentLibs.empty())
        newPath += ":" + parentLibs;
    if (existing && existing[0])
        newPath += ":";
    if (existing)
        newPath += existing;

    setenv("DYLD_LIBRARY_PATH", newPath.c_str(), 1);

#else
    (void)0; // 依赖 RPATH 或默认行为
#endif
}

} // namespace utils
} // namespace camel
