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

#pragma once

#include "../backend/backend.h"
#include "core/context/frame.h"
#include "core/rtdata/data.h"

class FastVMSchedPass;

namespace camel::jit {

struct JitContext {
    FastVMSchedPass *vm;
    const void *base; // Bytecode*
};

} // namespace camel::jit

// C linkage for JIT call from machine code
//
// 调用约定：callerSlots/slots 为动态区基址；slot[0] 恒存 Frame*（见 trampoline.cpp 头部注释）。
// trampolineFunc/TrampolineTail：callerSlots[0]=caller Frame，用于 get(operand)
// 支持静态区（负索引）。 trampolineOper：slots[0]=当前 Frame，直接使用 FrameArgsView，无需传
// Graph。
//
extern "C" slot_t trampolineFunc(slot_t *callerSlots, void *ctx, size_t pc);
extern "C" slot_t trampolineTail(slot_t *callerSlots, void *ctx, size_t pc);
extern "C" slot_t trampolineOper(slot_t *slots, void *ctx, size_t pc);

#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
/** Win64 asm trampoline: call fn(slots,ctx), store result, jmp to return_addr (avoids Release
 * shadow reuse). */
extern "C" void jit_call_trampoline(
    slot_t (*fn)(slot_t *slots, void *ctx), slot_t *slots, void *ctx, slot_t *result_out,
    void *return_addr);
#endif
