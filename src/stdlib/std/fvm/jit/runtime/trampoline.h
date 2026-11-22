// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../backend/backend.h"
#include "../mir/mir.h"
#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"

#include <cstddef>
#include <exception>

class FastVMSchedPass;

namespace camel::jit {

struct JitContext {
    FastVMSchedPass *vm;
    const void *base; // Bytecode*
    // Written by compiled code when it leaves through its fault exit (x64::JitFaultKind and the
    // bytecode pc of the fault), read and cleared by the C++ code that called it.
    uint32_t faultKind = 0;
    uint32_t faultPc   = 0;
    // Set by the C++ caller before it enters compiled code: how many calls the code may still
    // nest before the interpreter's recursion limit is reached. Kept in r15 while it runs.
    // A fault exit taken for a nearly full machine stack stores what was left of it here.
    uint64_t depthBudget = 0;
    // Set by the C++ caller with the budget: the lowest stack pointer at which compiled code may
    // still make a native call on the calling thread (execute::nativeStackFloor; 0 where the
    // bounds of the stack are unknown or the guard is switched off, which no stack pointer is
    // below). Compiled code compares rsp with it before every native call.
    uint64_t stackLimit = 0;
    // Framed code (see x64::JitExitKind). Written by the code when it leaves to its driver:
    // why, and the pc of the bytecode it left at.
    uint32_t exitKind = 0;
    uint32_t exitPc   = 0;
    // Set by the driver before it enters framed code: null to start at the entry of the graph,
    // or the address behind the exit the activation stopped at. The code writes that address
    // here when it leaves.
    uint64_t resumeAddr = 0;
    // The exception a helper caught (faultKind == PendingException), for the driver to throw.
    std::exception_ptr pending;
};
static_assert(offsetof(JitContext, resumeAddr) == x64::kJitCtxResumeAddrOffset);
static_assert(offsetof(JitContext, exitPc) == x64::kJitCtxExitPcOffset);
static_assert(offsetof(JitContext, exitKind) == x64::kJitCtxExitKindOffset);
static_assert(offsetof(JitContext, stackLimit) == x64::kJitCtxStackLimitOffset);
static_assert(offsetof(JitContext, depthBudget) == x64::kJitCtxDepthBudgetOffset);
static_assert(offsetof(JitContext, faultKind) == x64::kJitCtxFaultKindOffset);
static_assert(offsetof(JitContext, faultPc) == x64::kJitCtxFaultPcOffset);

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
extern "C" slot_t finishDirectJitCall(slot_t result, slot_t *calleeSlots, void *ctx);
extern "C" slot_t finishDirectJitCallFast(slot_t *calleeSlots, void *ctx, slot_t result);
extern "C" slot_t directSelfFuncInvoke(slot_t *callerSlots, void *ctx, const Bytecode *bc);

// Helpers framed code calls (MirOp::CallHelper). `slots` is the slot base of the activation's
// pool frame, slots[0] its Frame*. None of them throws: an exception is stored in
// JitContext::pending with faultKind = PendingException, and the code that called leaves through
// its fault exit. None of them calls a Camel function (those bytecodes are left to the driver).
// One of them reaches a safepoint: jitHelperLoopEnd, at the round boundary, where the loop's
// rounds are interruptible as they are under the interpreter.
// An operator that reaches a safepoint itself does so with the framed code that called
// jitHelperOper on the machine stack. That is allowed: the code holds nothing the collector
// would have to see or update (everything is in its pool frame, and it reads the frame again
// after the call). The collector's rule "no safepoint under compiled code" is a rule about
// frameless code, and the count that enforces it (mm::detail::nativeCodeDepth) is taken around
// the entry into frameless code only (docs/technical/36_gc_handle_rules.md, section 4). The loop
// event at a round boundary (jitHelperLoopEnd) polls the same safepoint with the same argument.
// OPER: calls the operator, stores and returns its result.
extern "C" slot_t jitHelperOper(slot_t *slots, void *ctx, size_t pc);
// CAST, COPY, ACCS, FILL, and a JOIN of more than two arms: runs the bytecode on the frame.
extern "C" slot_t jitHelperBytecode(slot_t *slots, void *ctx, size_t pc);
// BRCH with case values (match): the index of the arm taken, also stored in the result slot.
extern "C" slot_t jitHelperBranchArm(slot_t *slots, void *ctx, size_t pc);
// The three events of a loop of regions (camel/execute/loop_frame.h), on the frame of the
// activation exactly as the interpreters run them: LBEG starts a loop (its first state),
// LTST returns 1 when the round's condition stops the loop (or the iterable is exhausted) and
// otherwise binds the ports of the body, LEND returns 1 when another round follows (taking the
// body's result as the state and polled at the round safepoint). A loop's rounds stay
// interruptible at the safepoint FastVM has: LEND polls it, as the interpreter does.
extern "C" slot_t jitHelperLoopBegin(slot_t *slots, void *ctx, size_t pc);
extern "C" slot_t jitHelperLoopTest(slot_t *slots, void *ctx, size_t pc);
extern "C" slot_t jitHelperLoopEnd(slot_t *slots, void *ctx, size_t pc);

#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
/** Win64 asm trampoline: call fn(slots,ctx), store result, jmp to return_addr (avoids Release
 * shadow reuse). */
extern "C" void jit_call_trampoline(
    slot_t (*fn)(slot_t *slots, void *ctx), slot_t *slots, void *ctx, slot_t *result_out,
    void *return_addr);
#endif
