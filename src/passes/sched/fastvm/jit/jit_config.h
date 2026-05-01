/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Feb. 06, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
