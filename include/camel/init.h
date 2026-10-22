// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/utils/dll_path.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace camel {

/// Host-side initialization (SDK tools such as camel, camel-cpp, etc.): set up
/// library search paths and call this before using libcamel; idempotent.
/// Intended for executables running inside the SDK, with the exe directory as
/// the base (exe/, exe/libs, exe/../libs).
inline std::atomic<bool> &runtimeInitialized() {
    static std::atomic<bool> initialized{false};
    return initialized;
}

/// What a program or a tool writes to standard output and standard error is the same bytes on
/// every platform: a line ends with a line feed. On Windows the C runtime opens both in text
/// mode and writes CR LF for every line feed, which changes program output, JSON diagnostics
/// and everything that is compared with a recorded text. Standard input is left alone: a line
/// typed at a console ends with CR LF there, and text mode is what removes the CR.
inline void useBinaryStandardOutput() {
#ifdef _WIN32
    std::fflush(stdout);
    std::fflush(stderr);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
}

inline void initialize() {
    runtimeInitialized().store(true, std::memory_order_release);
    static std::once_flag once;
    std::call_once(once, []() {
        useBinaryStandardOutput();
        camel::utils::setupLibrarySearchPathForHost();
        auto installRoot = camel::utils::resolveInstallRoot();
        namespace fs     = std::filesystem;
        if (!fs::exists(installRoot / "bin") || !fs::exists(installRoot / "libs") ||
            !fs::exists(installRoot / "stdlib")) {
            std::cerr << "[camel] Warning: detected install root '"
                      << camel::utils::pathToUtf8(installRoot)
                      << "' misses one or more standard directories (bin/libs/stdlib)."
                      << " Set CAMEL_HOME to a valid SDK root if runtime loading fails.\n";
        }
    });
}

/// Host-side finalization: release global resources registered by initialize();
/// idempotent.
inline void finalize() {
    if (!runtimeInitialized().exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    // Keep a single finalization entry point for now; release future global
    // objects here as they are added.
}

/// Scope lifetime guard: call initialize() on construction and finalize() on destruction.
class ScopedRuntime {
  public:
    ScopedRuntime() { initialize(); }
    ~ScopedRuntime() { finalize(); }

    ScopedRuntime(const ScopedRuntime &)            = delete;
    ScopedRuntime &operator=(const ScopedRuntime &) = delete;
    ScopedRuntime(ScopedRuntime &&)                 = delete;
    ScopedRuntime &operator=(ScopedRuntime &&)      = delete;
};

} // namespace camel
