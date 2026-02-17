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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "backend.h"
#include "builtin/passes/sched/linear/fastvm/jit/jit_config.h"
#include "fallback.h"

#if JIT_TARGET_X64
#include "x64/x64_backend.h"
#endif

namespace camel::jit {

std::unique_ptr<IJitBackend> createBackend() {
#if JIT_TARGET_X64
    return std::make_unique<X64Backend>();
#else
    return std::make_unique<FallbackBackend>();
#endif
}

} // namespace camel::jit
