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
 * Created: Feb. 10, 2026
 * Updated: Feb. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * JIT 调试：在 Debug 模式下可在每条指令间插入对 jitDebugTrace 的调用，
 * 打印当前所有 GPR 与 pc，便于单步调试 JIT 执行。
 * Caller 只保存 rax、rcx 和 pc（call 仅改 rax/rcx）；callee 在入口将
 * rdx,r8..r11,rbx,rbp,rdi,rsi,r12..r15 写入 ctx。布局：0=r15..56=r8, 64=rdi,
 * 72=rsi, 80=rbp, 88=rbx, 96=rdx, 104=rcx, 112=rax, 120=pc。
 */

#pragma once

#include <cstdint>

namespace camel::jit {

struct JitDebugContext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t pc;
};

} // namespace camel::jit

extern "C" void jitDebugTrace(const void *ctx);

/** 供 JIT 调用：先将 ctx 拷入 thread_local 再调 jitDebugTrace，避免 stub 覆盖调用方栈上的保存区。
 */
extern "C" void jitDebugTraceWrapper(const void *ctx);

/** Release 用：与 jitDebugTraceWrapper 同签名，空实现；用于保证 Debug/Release 生成相同 JIT 码。
 */
extern "C" void jitDebugTraceNoOp(const void *ctx);
