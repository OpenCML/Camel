/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * 将优化后的 MIR buffer 编码为机器码字节流。当前实现为 x64：依赖 backend/x64 的 Encoder。
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
