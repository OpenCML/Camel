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
 * Created: Mar. 11, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/utils/dll_path.h"

#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace camel {

/// Host-side initialization (SDK tools such as camel, camel-cpp, etc.): set up
/// library search paths and call this before using libcamel; idempotent.
/// Intended for executables running inside the SDK, with the exe directory as
/// the base (exe/, exe/libs, exe/../libs).
inline std::atomic<bool> &runtimeInitialized() {
    static std::atomic<bool> initialized{false};
    return initialized;
}

inline void initialize() {
    runtimeInitialized().store(true, std::memory_order_release);
    static std::once_flag once;
    std::call_once(once, []() {
        camel::utils::setupLibrarySearchPathForHost();
        auto installRoot = camel::utils::resolveInstallRoot();
        namespace fs     = std::filesystem;
        if (!fs::exists(installRoot / "bin") || !fs::exists(installRoot / "libs") ||
            !fs::exists(installRoot / "stdlib")) {
            std::cerr << "[camel] Warning: detected install root '" << installRoot.string()
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
