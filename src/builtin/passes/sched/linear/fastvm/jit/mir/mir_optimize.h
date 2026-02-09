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
