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
 * Created: Feb. 09, 2026
 * Updated: Feb. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "builtin/passes/sched/linear/fastvm/jit/backend/x64/x64_encoder.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"

#include <unordered_map>
#include <vector>

namespace camel::jit::x64 {

// 将 buf 编码到 code，并可选输出汇编到 asmOut。pcToOffset 用于解析跳转目标（pc -> 字节偏移）。
void encodeMirBuffer(
    const MirBuffer &buf, const std::unordered_map<size_t, size_t> &pcToOffset,
    std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset = 0);

} // namespace camel::jit::x64
