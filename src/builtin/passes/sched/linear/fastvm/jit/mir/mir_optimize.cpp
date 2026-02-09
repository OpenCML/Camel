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

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_optimize.h"

namespace camel::jit::x64 {

void optimizeWin64RedundantArgSetup(MirBuffer &buf) {
    // 匹配 prologue 后紧跟的 mov rcx, rdi; mov rdx, rsi（Win64 下此时 rcx/rdx 未改，可删）
    for (size_t i = 0; i + 3 < buf.size(); ++i) {
        const Mir &a = buf[i];
        const Mir &b = buf[i + 1];
        const Mir &c = buf[i + 2];
        const Mir &d = buf[i + 3];
        if (a.op != MirOp::MovRegReg || a.r0 != kRegRdi || a.r1 != kRegRcx)
            continue;
        if (b.op != MirOp::MovRegReg || b.r0 != kRegRsi || b.r1 != kRegRdx)
            continue;
        if (c.op != MirOp::MovRegReg || c.r0 != kRegRcx || c.r1 != kRegRdi)
            continue;
        if (d.op != MirOp::MovRegReg || d.r0 != kRegRdx || d.r1 != kRegRsi)
            continue;
        // 删除 c 和 d（保留 pc 等信息到后续指令，此处两条无 pc 或可丢弃）
        buf.erase(
            buf.begin() + static_cast<std::ptrdiff_t>(i + 2),
            buf.begin() + static_cast<std::ptrdiff_t>(i + 4));
        return; // 只优化第一处（prologue 仅一段）
    }
}

void optimizeRemoveNoopMovRaxRax(MirBuffer &buf) {
    for (size_t i = 0; i < buf.size();) {
        const Mir &m = buf[i];
        if (m.op == MirOp::MovRegFromRax && m.r0 == kRegRax) {
            buf.erase(buf.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

} // namespace camel::jit::x64
