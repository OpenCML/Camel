// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The ONNX Runtime, opened at run time.
 *
 * This tree vendors no ONNX Runtime header (`onnxruntime_c_api` is nowhere in it), so the few
 * things this file needs from the C API are declared here: the two members of `OrtApiBase`, in
 * the order the official `onnxruntime_c_api.h` (ONNX Runtime 1.30) declares them, and the one
 * symbol the library exports. Every other entry of the C API is a field of the `OrtApi` struct,
 * which the step that creates a session declares as the prefix it needs -- the field order must
 * follow that official header exactly, because the pointer `GetApi` returns is a raw struct.
 *
 * `OrtGetApiBase` is the whole of the exported surface, so nothing else is looked up by name.
 *
 * Where the library is looked for is fixed, and the list is in the message a failed load hands
 * back, because that message is what tells a user where to put it. `CAMEL_HOME` locates an
 * installation and is used as such; the library's position is not a setting, and settings do not
 * live in environment variables (design principles 9, line 661), so there is no `CAMEL_*`
 * variable for it. "Install it wherever you like" is served by the platform's own loader: the
 * `PATH` on Windows, `LD_LIBRARY_PATH` on Linux.
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace camel::onnx::ort {

/// The API version this build asks for. ONNX Runtime answers only the version it was built for.
inline constexpr std::uint32_t kApiVersion = 22;
inline constexpr const char *kLibraryName =
#ifdef _WIN32
    "onnxruntime.dll";
#else
    "libonnxruntime.so";
#endif

/// The two members of the official `OrtApiBase`, in the official order.
struct OrtApiBase {
    const char *(*GetVersionString)();
    struct OrtApi *(*GetApi)(std::uint32_t version);
};

struct OrtApi;

using OrtGetApiBaseFn = const OrtApiBase *(*)();

/// The library names tried in one directory, most specific first: on Linux the soname the
/// linker records, then the development name. Windows DLLs carry no version suffix.
inline std::vector<std::string> libraryFileNames() {
#ifdef _WIN32
    return {kLibraryName};
#else
    return {"libonnxruntime.so.1", "libonnxruntime.so"};
#endif
}

/// Every path tried, in order: `$CAMEL_HOME/lib` (and, on Windows, `$CAMEL_HOME/bin`), then the
/// bare names the system loader resolves (`PATH` on Windows, `LD_LIBRARY_PATH` on Linux).
inline std::vector<std::filesystem::path> candidates() {
    std::vector<std::filesystem::path> paths;
    const char *home = std::getenv("CAMEL_HOME");
    if (home != nullptr && *home != '\0') {
        const std::filesystem::path root = std::filesystem::path(home);
        for (const std::string &name : libraryFileNames()) {
            paths.push_back(root / "lib" / name);
#ifdef _WIN32
            paths.push_back(root / "bin" / name);
#endif
        }
    }
    for (const std::string &name : libraryFileNames()) {
        paths.push_back(std::filesystem::path(name));
    }
    return paths;
}

/// The text of a failed load: what was looked for, and the two ways to make it found.
inline std::string failureMessage(const std::vector<std::filesystem::path> &tried) {
    std::string text = "onnx: cannot load ";
    text += kLibraryName;
    text += "; tried ";
    for (size_t i = 0; i < tried.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        text += tried[i].string();
    }
    text += ". Put the library in $CAMEL_HOME/lib (Windows: %CAMEL_HOME%\\bin is looked in too), "
            "or where the system loader finds it (Windows: PATH; Linux: LD_LIBRARY_PATH).";
    return text;
}

/// A loaded ONNX Runtime: the handle, the C API table, and the version the library reports.
///
/// Nothing here creates a session or a tensor; this step answers whether the runtime is there.
class Library {
  public:
    Library() = default;
    ~Library() { close(); }
    Library(const Library &)            = delete;
    Library &operator=(const Library &) = delete;

    /// Opens the first candidate that loads and exports `OrtGetApiBase`, and asks it for the C API
    /// table at `kApiVersion`. `false` means the runtime is not available here, and `message()`
    /// says why in the terms a user needs: the library, the paths tried, and where to put it.
    bool open() {
        const std::vector<std::filesystem::path> tried = candidates();
        for (const std::filesystem::path &path : tried) {
            void *handle = openOne(path);
            if (handle == nullptr) {
                continue;
            }
            const OrtGetApiBaseFn getApiBase =
                reinterpret_cast<OrtGetApiBaseFn>(findSymbol(handle, "OrtGetApiBase"));
            if (getApiBase == nullptr) {
                closeOne(handle);
                continue;
            }
            const OrtApiBase *base = getApiBase();
            if (base == nullptr || base->GetVersionString == nullptr || base->GetApi == nullptr) {
                closeOne(handle);
                continue;
            }
            if (base->GetApi(kApiVersion) == nullptr) {
                version_ = base->GetVersionString();
                closeOne(handle);
                message_ = "onnx: cannot load " + std::string(kLibraryName) +
                           "; the library found at " + path.string() + " reports version " +
                           version_ + ", which this build cannot drive (it needs API version " +
                           std::to_string(kApiVersion) + "). Install a matching ONNX Runtime.";
                return false;
            }
            handle_  = handle;
            apiBase_ = base;
            version_ = base->GetVersionString() != nullptr ? base->GetVersionString() : "";
            return true;
        }
        message_ = failureMessage(tried);
        return false;
    }

    bool isOpen() const { return handle_ != nullptr && apiBase_ != nullptr; }
    const std::string &version() const { return version_; }
    const std::string &message() const { return message_; }

  private:
    static void *openOne(const std::filesystem::path &path) {
#ifdef _WIN32
        return reinterpret_cast<void *>(LoadLibraryW(path.wstring().c_str()));
#else
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }
    static void *findSymbol(void *handle, const char *name) {
#ifdef _WIN32
        return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
        return dlsym(handle, name);
#endif
    }
    static void closeOne(void *handle) {
#ifdef _WIN32
        FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
    void close() {
        if (handle_ != nullptr) {
            closeOne(handle_);
            handle_ = nullptr;
        }
        apiBase_ = nullptr;
    }

    void *handle_              = nullptr;
    const OrtApiBase *apiBase_ = nullptr;
    std::string version_;
    std::string message_;
};

} // namespace camel::onnx::ort