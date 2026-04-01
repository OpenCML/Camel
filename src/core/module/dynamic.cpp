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
 * Created: Feb. 20, 2026
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/module/dynamic.h"
#include "camel/core/context/context.h"
#include "camel/utils/dll_path.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

namespace fs = std::filesystem;

void *openDll(const std::string &path) {
#ifdef _WIN32
    return static_cast<void *>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeDll(void *handle) {
    if (!handle)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

#ifdef _WIN32
std::string lastDlErrorString() {
    DWORD code = GetLastError();
    if (code == 0) {
        return "unknown loader error";
    }
    LPWSTR wide = nullptr;
    DWORD len   = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&wide),
        0,
        nullptr);
    std::wstring ws = (len > 0 && wide != nullptr) ? std::wstring(wide, len) : L"";
    if (wide) {
        LocalFree(wide);
    }
    ws.erase(std::remove(ws.begin(), ws.end(), L'\r'), ws.end());
    ws.erase(std::remove(ws.begin(), ws.end(), L'\n'), ws.end());
    std::string msg;
    if (!ws.empty()) {
        int size = WideCharToMultiByte(
            CP_UTF8,
            0,
            ws.c_str(),
            static_cast<int>(ws.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (size > 0) {
            msg.resize(static_cast<size_t>(size));
            WideCharToMultiByte(
                CP_UTF8,
                0,
                ws.c_str(),
                static_cast<int>(ws.size()),
                msg.data(),
                size,
                nullptr,
                nullptr);
        }
    }
    std::ostringstream os;
    os << "Win32Error(" << code << "): " << (msg.empty() ? "unknown error" : msg);
    return os.str();
}
#else
std::string lastDlErrorString() {
    const char *err = dlerror();
    return err ? std::string(err) : std::string("unknown loader error");
}
#endif

std::vector<fs::path> collectPythonCandidateDirs() {
    std::vector<fs::path> dirs;
    dirs.push_back(fs::current_path() / "modules" / "python" / "dlls");
    auto installRoot = camel::utils::resolveInstallRoot();
    dirs.push_back(installRoot / "modules" / "python" / "dlls");

    std::string venv = getEnv("VIRTUAL_ENV");
    if (venv.empty()) {
        venv = getEnv("CONDA_PREFIX");
    }
    if (!venv.empty()) {
        fs::path root = fs::path(venv);
        dirs.push_back(root);
#ifdef _WIN32
        dirs.push_back(root / "Scripts");
        dirs.push_back(root / "DLLs");
#else
        dirs.push_back(root / "bin");
        dirs.push_back(root / "lib");
#endif
    }
    return dirs;
}

std::optional<std::string> parsePythonVersionTag(const std::string &versionLike) {
    int major = -1;
    int minor = -1;
    char dot  = 0;
    std::stringstream ss(versionLike);
    if (!(ss >> major >> dot >> minor) || dot != '.') {
        return std::nullopt;
    }
    if (major < 0 || minor < 0) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << major;
    if (minor < 10) {
        out << '0';
    }
    out << minor;
    return out.str();
}

std::optional<std::string> detectPythonVersionFromVenvCfg(const fs::path &venvRoot) {
    if (venvRoot.empty()) {
        return std::nullopt;
    }
    std::ifstream in((venvRoot / "pyvenv.cfg").string());
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("version", 0) != 0) {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string value = line.substr(pos + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        auto tag = parsePythonVersionTag(value);
        if (tag.has_value()) {
            return tag;
        }
    }
    return std::nullopt;
}

std::optional<std::string> detectPythonVersionFromDllName(const fs::path &dir) {
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        return std::nullopt;
    }
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("python", 0) == 0 && lower.size() >= 11 &&
            lower.substr(lower.size() - 4) == ".dll") {
            std::string digits = lower.substr(6, lower.size() - 10); // python + digits + .dll
            if (digits.size() == 3 && digits[0] == '3') {
                return digits; // e.g. 311
            }
            if (digits.size() == 2 && digits[0] == '3') {
                return std::string("3") + "0" + digits.substr(1); // e.g. 39 -> 309
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> detectActivePythonVersionTag() {
    std::string venv = getEnv("VIRTUAL_ENV");
    if (!venv.empty()) {
        auto fromCfg = detectPythonVersionFromVenvCfg(fs::path(venv));
        if (fromCfg.has_value()) {
            return fromCfg;
        }
        auto fromDll = detectPythonVersionFromDllName(fs::path(venv));
        if (fromDll.has_value()) {
            return fromDll;
        }
        auto fromScripts = detectPythonVersionFromDllName(fs::path(venv) / "Scripts");
        if (fromScripts.has_value()) {
            return fromScripts;
        }
    }
    std::string conda = getEnv("CONDA_PREFIX");
    if (!conda.empty()) {
        auto fromRoot = detectPythonVersionFromDllName(fs::path(conda));
        if (fromRoot.has_value()) {
            return fromRoot;
        }
        auto fromBin = detectPythonVersionFromDllName(fs::path(conda) / "Library" / "bin");
        if (fromBin.has_value()) {
            return fromBin;
        }
    }
    return std::nullopt;
}

std::string bridgeFileNameForTag(const std::string &tag) {
#ifdef _WIN32
    return "py_bridge" + tag + ".dll";
#elif defined(__APPLE__)
    return "libpy_bridge" + tag + ".dylib";
#else
    return "libpy_bridge" + tag + ".so";
#endif
}

std::vector<std::string> listAvailableBridgeTags(const fs::path &dir) {
    std::vector<std::string> out;
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        return out;
    }
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
#ifdef _WIN32
        if (lower.rfind("py_bridge3", 0) == 0 && lower.size() > 13 &&
            lower.substr(lower.size() - 4) == ".dll") {
            out.push_back(lower.substr(9, lower.size() - 13));
        }
#elif defined(__APPLE__)
        if (lower.rfind("libpy_bridge3", 0) == 0 && lower.size() > 16 &&
            lower.substr(lower.size() - 6) == ".dylib") {
            out.push_back(lower.substr(12, lower.size() - 18));
        }
#else
        if (lower.rfind("libpy_bridge3", 0) == 0 && lower.size() > 13 &&
            lower.substr(lower.size() - 3) == ".so") {
            out.push_back(lower.substr(12, lower.size() - 15));
        }
#endif
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool tryLoadPythonBridgeForActiveEnv(
    const camel::core::context::context_ptr_t &ctx, std::string *outError) {
    auto activeTag = detectActivePythonVersionTag();
    if (!activeTag.has_value()) {
        CAMEL_LOG_INFO_S(
            "Context",
            "run | python bridge | no env tag (venv/conda/python) -> legacy .cmo load");
        return true; // No active env info; keep legacy behavior.
    }
    fs::path installRoot = camel::utils::resolveInstallRoot();
    fs::path libsDir     = installRoot / "libs";
    fs::path bridgePath  = libsDir / bridgeFileNameForTag(activeTag.value());
    if (!fs::exists(bridgePath)) {
        std::vector<std::string> avail;
        camel::log::info_lazy("Context", [&] {
            if (avail.empty()) {
                avail = listAvailableBridgeTags(libsDir);
            }
            return std::format(
                "run | python bridge | tag {} | missing {} | other bridge tags under libs: {}",
                activeTag.value(),
                bridgePath.string(),
                avail.size());
        });
        if (outError) {
            if (avail.empty()) {
                avail = listAvailableBridgeTags(libsDir);
            }
            std::ostringstream os;
            os << "python bridge for active environment not found: " << bridgePath.string();
            if (!avail.empty()) {
                os << ". Available bridge tag(s): ";
                for (size_t i = 0; i < avail.size(); ++i) {
                    os << (i == 0 ? "" : ", ") << avail[i];
                }
            }
            *outError = os.str();
        }
        return false;
    }
    void *bridge = openDll(bridgePath.string());
    if (!bridge) {
        if (outError) {
            *outError = "failed to load python bridge: " + bridgePath.string() + " (" +
                        lastDlErrorString() + ")";
        }
        return false;
    }
    CAMEL_LOG_INFO_S(
        "Context",
        "run | python bridge | tag {} | loaded {}",
        activeTag.value(),
        bridgePath.string());
    ctx->addLoadedDll(bridgePath.string(), bridge);
    return true;
}

void addRuntimeLibrarySearchPath(const fs::path &dir, std::set<std::string> &seen) {
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    fs::path abs = fs::absolute(dir, ec).lexically_normal();
    if (ec || !fs::exists(abs) || !fs::is_directory(abs)) {
        return;
    }
    std::string key = abs.string();
    if (!seen.insert(key).second) {
        return;
    }
    camel::utils::addLibrarySearchPath(abs);
}

void registerRuntimeSearchDirs(const std::string &moduleName, const std::string &cmoPath) {
    std::set<std::string> seen;
    const fs::path cmo = fs::path(cmoPath);
    addRuntimeLibrarySearchPath(cmo.parent_path(), seen);

    fs::path installRoot = camel::utils::resolveInstallRoot();
    addRuntimeLibrarySearchPath(installRoot / "libs", seen);
    addRuntimeLibrarySearchPath(installRoot / "bin", seen);
    addRuntimeLibrarySearchPath(installRoot, seen);

    if (moduleName == "python" || moduleName == "pyplot") {
        for (const auto &dir : collectPythonCandidateDirs()) {
            addRuntimeLibrarySearchPath(dir, seen);
        }
    }
}

template <typename F> F getSymbol(void *handle, const char *name) {
#ifdef _WIN32
    return reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return reinterpret_cast<F>(dlsym(handle, name));
#endif
}

} // namespace

using namespace camel::core::context;

namespace camel::core::module {

module_ptr_t loadCmoModule(
    const std::string &moduleName, const std::string &path,
    const camel::core::context::context_ptr_t &ctx, std::string *outError) {
    auto setErr = [&](const std::string &s) {
        if (outError)
            *outError = s;
    };
    registerRuntimeSearchDirs(moduleName, path);
    if (moduleName == "python" || moduleName == "pyplot") {
        std::string bridgeErr;
        if (!tryLoadPythonBridgeForActiveEnv(ctx, &bridgeErr)) {
            setErr(bridgeErr);
            return nullptr;
        }
    }
    void *handle = openDll(path);
    if (!handle) {
        CAMEL_LOG_WARN_S("Context", "Failed to load .cmo: {}", path);
        setErr(
            "failed to load DLL: " + path + " (" + lastDlErrorString() +
            "). Check dependent runtime DLLs (e.g. python3xx.dll) and search paths.");
        return nullptr;
    }

    using AbiVersionFn = int (*)();
    auto abiVersionFn  = getSymbol<AbiVersionFn>(handle, "camel_module_abi_version");
    if (!abiVersionFn) {
        CAMEL_LOG_WARN_S(
            "Context",
            "camel_module_abi_version not found in: {} (rebuild .cmo with current SDK)",
            path);
        closeDll(handle);
        setErr(
            "camel_module_abi_version not found in " + path + " (rebuild .cmo with current SDK)");
        return nullptr;
    }
    int moduleAbi = abiVersionFn();
    if (moduleAbi != CAMEL_MODULE_ABI_VERSION) {
        CAMEL_LOG_WARN_S(
            "Context",
            "Module ABI version mismatch: {} has version {}, host expects {}; skip loading",
            path,
            moduleAbi,
            CAMEL_MODULE_ABI_VERSION);
        closeDll(handle);
        setErr(
            "ABI version mismatch: .cmo has version " + std::to_string(moduleAbi) +
            ", host expects " + std::to_string(CAMEL_MODULE_ABI_VERSION));
        return nullptr;
    }

    auto factory = getSymbol<CamelModuleFactory>(handle, "camel_module_create");
    if (!factory) {
        CAMEL_LOG_WARN_S("Context", "camel_module_create not found in: {}", path);
        closeDll(handle);
        setErr("camel_module_create not found in " + path);
        return nullptr;
    }

    Module *raw = factory(ctx.get());
    if (!raw) {
        closeDll(handle);
        setErr("camel_module_create returned null for " + path);
        return nullptr;
    }

    ctx->addLoadedDll(path, handle);
    return std::shared_ptr<Module>(raw);
}

} // namespace camel::core::module
