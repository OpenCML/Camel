// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * JIT debug: in Debug mode, insert a call to jitDebugTrace between instructions
 * to print all current GPRs and pc for single-step JIT debugging.
 * The caller saves only rax, rcx, and pc (call only changes rax/rcx); the callee writes
 * rdx, r8..r11, rbx, rbp, rdi, rsi, r12..r15 into ctx. Layout: 0=r15..56=r8, 64=rdi,
 * 72=rsi, 80=rbp, 88=rbx, 96=rdx, 104=rcx, 112=rax, 120=pc.
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

/** For JIT calls: copy ctx into thread_local first, then call jitDebugTrace, so the stub does not
 * clobber the caller's stack save area. */
extern "C" void jitDebugTraceWrapper(const void *ctx);

/** Release build: same signature as jitDebugTraceWrapper, but a no-op. Used to ensure Debug and
 * Release generate identical JIT code. */
extern "C" void jitDebugTraceNoOp(const void *ctx);
