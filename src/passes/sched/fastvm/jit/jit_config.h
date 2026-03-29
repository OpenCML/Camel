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
 * Updated: Mar. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>

// ENABLE_FASTVM_JIT、ENABLE_FASTVM_COMPUTED_GOTO、JIT_TARGET_X64 由 CMake 统一设置。
// 支持: cmake -DENABLE_FASTVM_JIT=OFF -DENABLE_FASTVM_COMPUTED_GOTO=OFF

namespace camel::jit {

enum class JitPolicy {
    Disabled, // 始终解释执行
    OnDemand, // 热点触发 JIT
    Always,   // 首次触达即 JIT（不做启动期全量编译）
};

struct JitConfig {
    JitPolicy policy        = JitPolicy::OnDemand;
    size_t hotThreshold     = 1;
    size_t maxCodeCacheSize = 4 * 1024 * 1024; // 4MB
};

} // namespace camel::jit
