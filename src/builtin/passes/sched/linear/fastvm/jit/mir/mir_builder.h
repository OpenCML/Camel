/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * 将字节码对应的机器指令序列构建为 MIR buffer，供优化与编码使用。
 */

#pragma once

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"

namespace camel::jit::x64 {

class MirBuilder {
  public:
    explicit MirBuilder(MirBuffer &buf) : buf_(buf) {}

    // 下一条 push 的 MIR 将带上此 pc（用于 pcToOffset 与 debug）
    void setNextPc(uint32_t pc) { nextPc_ = pc; }

    void emitPrologueWin64() {
        push(MirOp::MovRegReg, kRegRdi, kRegRcx);
        push(MirOp::MovRegReg, kRegRsi, kRegRdx);
    }

    void emitMovRegReg(uint8_t dst, uint8_t src) { push(MirOp::MovRegReg, dst, src); }
    void emitMovRegImm32(uint8_t reg, uint32_t imm32) {
        Mir m;
        m.op    = MirOp::MovRegImm32;
        m.r0    = reg;
        m.imm32 = imm32;
        push(m);
    }
    void emitMovRegImm64(uint8_t reg, uint64_t imm64) {
        Mir m;
        m.op    = MirOp::MovRegImm64;
        m.r0    = reg;
        m.imm64 = imm64;
        push(m);
    }
    void emitMovRegFromFrame(uint8_t reg, int disp) {
        Mir m;
        m.op   = MirOp::MovRegFromFrame;
        m.r0   = reg;
        m.disp = disp;
        push(m);
    }
    void emitMovFrameFromReg(int disp, uint8_t reg) {
        Mir m;
        m.op   = MirOp::MovFrameFromReg;
        m.disp = disp;
        m.r0   = reg;
        push(m);
    }
    void emitMovRaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MovRaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitAddRaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::AddRaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitSubRaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::SubRaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitMovRaxFromReg(uint8_t reg) { push(MirOp::MovRaxFromReg, reg, 0); }
    void emitMovRegFromRax(uint8_t reg) { push(MirOp::MovRegFromRax, reg, 0); }
    void emitAddRaxFromReg(uint8_t reg) { push(MirOp::AddRaxFromReg, reg, 0); }
    void emitSubRaxFromReg(uint8_t reg) { push(MirOp::SubRaxFromReg, reg, 0); }
    void emitAddRaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::AddRaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitSubRaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::SubRaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMovXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MovXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitAddXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::AddXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitSubXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::SubXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMovFrameFromXmm0(int disp) {
        Mir m;
        m.op   = MirOp::MovFrameFromXmm0;
        m.disp = disp;
        push(m);
    }
    void emitMovXmm0FromReg(uint8_t reg) { push(MirOp::MovXmm0FromReg, reg, 0); }
    void emitAddXmm0FromReg(uint8_t reg) { push(MirOp::AddXmm0FromReg, reg, 0); }
    void emitSubXmm0FromReg(uint8_t reg) { push(MirOp::SubXmm0FromReg, reg, 0); }
    void emitMovRegFromXmm0(uint8_t reg) { push(MirOp::MovRegFromXmm0, reg, 0); }
    void emitMovXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MovXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitAddXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::AddXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitSubXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::SubXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpRaxImm8Setle() { push(MirOp::CmpRaxImm8Setle, 0, 0); }
    void emitTestRaxRax() { push(MirOp::TestRaxRax, 0, 0); }
    void emitTestRaxJzRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::TestRaxJzRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitCmoveRcxFromRbx() { push(MirOp::CmoveRcxFromRbx, 0, 0); }
    void emitJmpRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JmpRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitJleRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JleRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitJmpRel8(int32_t rel) {
        Mir m;
        m.op   = MirOp::JmpRel8;
        m.disp = rel;
        push(m);
    }
    void emitJleRel8(int32_t rel) {
        Mir m;
        m.op   = MirOp::JleRel8;
        m.disp = rel;
        push(m);
    }
    void emitRet() { push(MirOp::Ret, 0, 0); }
    void emitCallRax() { push(MirOp::CallRax, 0, 0); }

    // Win64: 准备 trampoline(slots, ctx, pc) 并 call rax。优化 pass 可删前两条 mov。
    void emitCallTrampolineWin64(uint32_t pc, uint64_t trampolineAddr) {
        emitMovRegReg(kRegRcx, kRegRdi);
        emitMovRegReg(kRegRdx, kRegRsi);
        emitMovRegImm32(kRegR8, pc);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
    }
    void emitCallTrampolineOperWin64(uint32_t pc, uint64_t graphPtr, uint64_t trampolineAddr) {
        emitMovRegReg(kRegRcx, kRegRdi);
        emitMovRegReg(kRegRdx, kRegRsi);
        emitMovRegImm32(kRegR8, pc);
        emitMovRegImm64(kRegR9, graphPtr);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
    }

    // SysV：rdi/rsi 已是 slots/ctx，设 rdx=pc、rax=addr 后 call
    void emitCallTrampolineSysV(uint32_t pc, uint64_t trampolineAddr) {
        emitMovRegImm32(kRegRdx, pc);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
    }
    // SysV：第 3 参 pc→rdx，第 4 参 graph→rcx，rax=addr，call
    void emitCallTrampolineOperSysV(uint32_t pc, uint64_t graphPtr, uint64_t trampolineAddr) {
        emitMovRegImm32(kRegRdx, pc);
        emitMovRegImm64(kRegRcx, graphPtr);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
    }

  private:
    MirBuffer &buf_;
    uint32_t nextPc_{kMirInvalidPc};

    void push(MirOp op, uint8_t r0, uint8_t r1) {
        Mir m;
        m.op = op;
        m.r0 = r0;
        m.r1 = r1;
        push(m);
    }
    void push(Mir m) {
        if (nextPc_ != kMirInvalidPc) {
            m.pc    = nextPc_;
            nextPc_ = kMirInvalidPc;
        }
        buf_.push_back(m);
    }
};

} // namespace camel::jit::x64
