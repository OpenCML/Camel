// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

// ENABLE_FASTVM_JIT, ENABLE_FASTVM_COMPUTED_GOTO, and JIT_TARGET_X64 are set centrally by CMake.
// Example: cmake -DENABLE_FASTVM_JIT=OFF -DENABLE_FASTVM_COMPUTED_GOTO=OFF

namespace camel::jit {

enum class JitPolicy {
    Disabled, // Always interpret.
    OnDemand, // Trigger JIT on hot spots.
    Always,   // JIT on first hit (no full startup compilation).
};

struct JitConfig {
    JitPolicy policy        = JitPolicy::OnDemand;
    size_t hotThreshold     = 1;
    size_t maxCodeCacheSize = 4 * 1024 * 1024; // 4MB
};

} // namespace camel::jit
