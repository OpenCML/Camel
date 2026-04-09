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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../backend/backend.h"
#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"

class FastVMSchedPass;

namespace camel::jit {

struct JitContext {
    FastVMSchedPass *vm;
    const void *base; // Bytecode*
};

} // namespace camel::jit

// C linkage for JIT call from machine code
//
// Calling convention: callerSlots/slots point to the dynamic-area base; slot[0] always stores
// Frame* (see the trampoline.cpp header comment).
// trampolineFunc/TrampolineTail: callerSlots[0] = caller Frame, used by get(operand).
// Supports the static area (negative indices). trampolineOper: slots[0] = current Frame, uses
// FrameArgsView directly, no Graph copy required.
extern "C" slot_t trampolineFunc(slot_t *callerSlots, void *ctx, size_t pc);
extern "C" slot_t trampolineTail(slot_t *callerSlots, void *ctx, size_t pc);
extern "C" slot_t trampolineOper(slot_t *slots, void *ctx, size_t pc);
extern "C" slot_t trampolineCast(slot_t *slots, void *ctx, size_t pc);
extern "C" slot_t trampolineBytecode(slot_t *slots, void *ctx, size_t pc);
extern "C" slot_t *prepareDirectJitCall(slot_t *callerSlots, void *ctx, const Bytecode *bc);
extern "C" slot_t *prepareDirectJitTailCall(slot_t *callerSlots, void *ctx, const Bytecode *bc);
extern "C" slot_t
finishDirectJitCall(slot_t result, slot_t *calleeSlots, void *ctx, GIR::Graph *owner);
extern "C" slot_t finishDirectJitCallFast(slot_t *calleeSlots, void *ctx, slot_t result);
extern "C" slot_t directSelfFuncInvoke(slot_t *callerSlots, void *ctx, const Bytecode *bc);

#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
/** Win64 asm trampoline: call fn(slots,ctx), store result, jmp to return_addr (avoids Release
 * shadow reuse). */
extern "C" void jit_call_trampoline(
    slot_t (*fn)(slot_t *slots, void *ctx), slot_t *slots, void *ctx, slot_t *result_out,
    void *return_addr);
#endif
