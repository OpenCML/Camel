/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * MIR 优化 pass，为 L3 更多优化预留扩展点。
 */

#pragma once

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"

namespace camel::jit::x64 {

// Win64：删除 prologue 后紧跟的「mov rcx, rdi; mov rdx, rsi」（此时 rcx/rdx 未改，冗余）
void optimizeWin64RedundantArgSetup(MirBuffer &buf);

// 删除 no-op：mov rax, rax（MovRegFromRax(0)）
void optimizeRemoveNoopMovRaxRax(MirBuffer &buf);

// 入口：可在此串联多个 pass，便于后续加死代码删除、peephole 等
inline void optimizeMirBuffer(MirBuffer &buf) {
    optimizeWin64RedundantArgSetup(buf);
    optimizeRemoveNoopMovRaxRax(buf);
}

} // namespace camel::jit::x64
