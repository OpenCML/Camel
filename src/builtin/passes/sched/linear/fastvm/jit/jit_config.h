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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>

#ifndef ENABLE_JIT
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
#define ENABLE_JIT 1
#else
#define ENABLE_JIT 0
#endif
#endif

#if ENABLE_JIT
#define JIT_TARGET_X64 1
#else
#define JIT_TARGET_X64 0
#endif

namespace camel::jit {

enum class JitPolicy {
    Disabled, // 始终解释执行
    OnDemand, // 热点触发 JIT
    Always,   // 启动时全量 JIT
};

struct JitConfig {
    JitPolicy policy        = JitPolicy::OnDemand;
    size_t hotThreshold     = 1000;
    size_t maxCodeCacheSize = 4 * 1024 * 1024; // 4MB
};

} // namespace camel::jit
