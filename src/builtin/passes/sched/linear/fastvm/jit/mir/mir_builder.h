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
    void emitCmpRaxFrameSetle(int disp) {
        Mir m;
        m.op   = MirOp::CmpRaxFrameSetle;
        m.disp = disp;
        push(m);
    }
    void emitCmpRaxMemAtSetle(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpRaxMemAtSetle;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpRaxFrameSetl(int disp) {
        Mir m;
        m.op   = MirOp::CmpRaxFrameSetl;
        m.disp = disp;
        push(m);
    }
    void emitCmpRaxMemAtSetl(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpRaxMemAtSetl;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpRaxFrameSetg(int disp) {
        Mir m;
        m.op   = MirOp::CmpRaxFrameSetg;
        m.disp = disp;
        push(m);
    }
    void emitCmpRaxMemAtSetg(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpRaxMemAtSetg;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpRaxFrameSete(int disp) {
        Mir m;
        m.op   = MirOp::CmpRaxFrameSete;
        m.disp = disp;
        push(m);
    }
    void emitCmpRaxMemAtSete(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpRaxMemAtSete;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpRaxFrameSetne(int disp) {
        Mir m;
        m.op   = MirOp::CmpRaxFrameSetne;
        m.disp = disp;
        push(m);
    }
    void emitCmpRaxMemAtSetne(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpRaxMemAtSetne;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpRaxFrameSetge(int disp) {
        Mir m;
        m.op   = MirOp::CmpRaxFrameSetge;
        m.disp = disp;
        push(m);
    }
    void emitCmpRaxMemAtSetge(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpRaxMemAtSetge;
        m.imm64 = addr;
        push(m);
    }
    void emitMulRaxFromReg(uint8_t reg) { push(MirOp::MulRaxFromReg, reg, 0); }
    void emitMulRaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MulRaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMulRaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MulRaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitIdivRaxByReg(uint8_t reg) { push(MirOp::IdivRaxByReg, reg, 0); }
    void emitIdivRaxByFrame(int disp) {
        Mir m;
        m.op   = MirOp::IdivRaxByFrame;
        m.disp = disp;
        push(m);
    }
    void emitIdivRaxByMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::IdivRaxByMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitMulXmm0FromReg(uint8_t reg) { push(MirOp::MulXmm0FromReg, reg, 0); }
    void emitMulXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MulXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMulXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MulXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitDivXmm0FromReg(uint8_t reg) { push(MirOp::DivXmm0FromReg, reg, 0); }
    void emitDivXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::DivXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitDivXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::DivXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0FrameSetb(int disp) {
        Mir m;
        m.op   = MirOp::ComisdXmm0FrameSetb;
        m.disp = disp;
        push(m);
    }
    void emitComisdXmm0MemAtSetb(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComisdXmm0MemAtSetb;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0RegSetb(uint8_t reg) { push(MirOp::ComisdXmm0RegSetb, reg, 0); }
    void emitComisdXmm0FrameSetbe(int disp) {
        Mir m;
        m.op   = MirOp::ComisdXmm0FrameSetbe;
        m.disp = disp;
        push(m);
    }
    void emitComisdXmm0MemAtSetbe(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComisdXmm0MemAtSetbe;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0RegSetbe(uint8_t reg) { push(MirOp::ComisdXmm0RegSetbe, reg, 0); }
    void emitComisdXmm0FrameSete(int disp) {
        Mir m;
        m.op   = MirOp::ComisdXmm0FrameSete;
        m.disp = disp;
        push(m);
    }
    void emitComisdXmm0MemAtSete(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComisdXmm0MemAtSete;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0RegSete(uint8_t reg) { push(MirOp::ComisdXmm0RegSete, reg, 0); }
    void emitComisdXmm0FrameSeta(int disp) {
        Mir m;
        m.op   = MirOp::ComisdXmm0FrameSeta;
        m.disp = disp;
        push(m);
    }
    void emitComisdXmm0MemAtSeta(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComisdXmm0MemAtSeta;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0RegSeta(uint8_t reg) { push(MirOp::ComisdXmm0RegSeta, reg, 0); }
    void emitComisdXmm0FrameSetae(int disp) {
        Mir m;
        m.op   = MirOp::ComisdXmm0FrameSetae;
        m.disp = disp;
        push(m);
    }
    void emitComisdXmm0MemAtSetae(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComisdXmm0MemAtSetae;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0RegSetae(uint8_t reg) { push(MirOp::ComisdXmm0RegSetae, reg, 0); }
    void emitComisdXmm0FrameSetnz(int disp) {
        Mir m;
        m.op   = MirOp::ComisdXmm0FrameSetnz;
        m.disp = disp;
        push(m);
    }
    void emitComisdXmm0MemAtSetnz(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComisdXmm0MemAtSetnz;
        m.imm64 = addr;
        push(m);
    }
    void emitComisdXmm0RegSetnz(uint8_t reg) { push(MirOp::ComisdXmm0RegSetnz, reg, 0); }
    // 32 位整型 (I*)
    void emitMovEaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MovEaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMovEaxFromReg(uint8_t reg) { push(MirOp::MovEaxFromReg, reg, 0); }
    void emitMovEaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MovEaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitAddEaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::AddEaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitAddEaxFromReg(uint8_t reg) { push(MirOp::AddEaxFromReg, reg, 0); }
    void emitAddEaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::AddEaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitSubEaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::SubEaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitSubEaxFromReg(uint8_t reg) { push(MirOp::SubEaxFromReg, reg, 0); }
    void emitSubEaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::SubEaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitMulEaxFromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MulEaxFromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMulEaxFromReg(uint8_t reg) { push(MirOp::MulEaxFromReg, reg, 0); }
    void emitMulEaxFromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MulEaxFromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitIdivEaxByFrame(int disp) {
        Mir m;
        m.op   = MirOp::IdivEaxByFrame;
        m.disp = disp;
        push(m);
    }
    void emitIdivEaxByReg(uint8_t reg) { push(MirOp::IdivEaxByReg, reg, 0); }
    void emitIdivEaxByMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::IdivEaxByMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxFrameSetl(int disp) {
        Mir m;
        m.op   = MirOp::CmpEaxFrameSetl;
        m.disp = disp;
        push(m);
    }
    void emitCmpEaxMemAtSetl(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpEaxMemAtSetl;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxRegSetl(uint8_t reg) { push(MirOp::CmpEaxRegSetl, reg, 0); }
    void emitCmpEaxFrameSetg(int disp) {
        Mir m;
        m.op   = MirOp::CmpEaxFrameSetg;
        m.disp = disp;
        push(m);
    }
    void emitCmpEaxMemAtSetg(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpEaxMemAtSetg;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxRegSetg(uint8_t reg) { push(MirOp::CmpEaxRegSetg, reg, 0); }
    void emitCmpEaxFrameSete(int disp) {
        Mir m;
        m.op   = MirOp::CmpEaxFrameSete;
        m.disp = disp;
        push(m);
    }
    void emitCmpEaxMemAtSete(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpEaxMemAtSete;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxRegSete(uint8_t reg) { push(MirOp::CmpEaxRegSete, reg, 0); }
    void emitCmpEaxFrameSetne(int disp) {
        Mir m;
        m.op   = MirOp::CmpEaxFrameSetne;
        m.disp = disp;
        push(m);
    }
    void emitCmpEaxMemAtSetne(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpEaxMemAtSetne;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxRegSetne(uint8_t reg) { push(MirOp::CmpEaxRegSetne, reg, 0); }
    void emitCmpEaxFrameSetle(int disp) {
        Mir m;
        m.op   = MirOp::CmpEaxFrameSetle;
        m.disp = disp;
        push(m);
    }
    void emitCmpEaxMemAtSetle(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpEaxMemAtSetle;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxRegSetle(uint8_t reg) { push(MirOp::CmpEaxRegSetle, reg, 0); }
    void emitCmpEaxFrameSetge(int disp) {
        Mir m;
        m.op   = MirOp::CmpEaxFrameSetge;
        m.disp = disp;
        push(m);
    }
    void emitCmpEaxMemAtSetge(uint64_t addr) {
        Mir m;
        m.op    = MirOp::CmpEaxMemAtSetge;
        m.imm64 = addr;
        push(m);
    }
    void emitCmpEaxRegSetge(uint8_t reg) { push(MirOp::CmpEaxRegSetge, reg, 0); }
    // 32 位浮点 (F*)
    void emitMovSsXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MovSsXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMovSsXmm0FromReg(uint8_t reg) { push(MirOp::MovSsXmm0FromReg, reg, 0); }
    void emitMovSsXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MovSsXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitAddSsXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::AddSsXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitAddSsXmm0FromReg(uint8_t reg) { push(MirOp::AddSsXmm0FromReg, reg, 0); }
    void emitAddSsXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::AddSsXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitSubSsXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::SubSsXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitSubSsXmm0FromReg(uint8_t reg) { push(MirOp::SubSsXmm0FromReg, reg, 0); }
    void emitSubSsXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::SubSsXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitMulSsXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::MulSsXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitMulSsXmm0FromReg(uint8_t reg) { push(MirOp::MulSsXmm0FromReg, reg, 0); }
    void emitMulSsXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::MulSsXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitDivSsXmm0FromFrame(int disp) {
        Mir m;
        m.op   = MirOp::DivSsXmm0FromFrame;
        m.disp = disp;
        push(m);
    }
    void emitDivSsXmm0FromReg(uint8_t reg) { push(MirOp::DivSsXmm0FromReg, reg, 0); }
    void emitDivSsXmm0FromMemAt(uint64_t addr) {
        Mir m;
        m.op    = MirOp::DivSsXmm0FromMemAt;
        m.imm64 = addr;
        push(m);
    }
    void emitMovSsFrameFromXmm0(int disp) {
        Mir m;
        m.op   = MirOp::MovSsFrameFromXmm0;
        m.disp = disp;
        push(m);
    }
    void emitMovSsRegFromXmm0(uint8_t reg) { push(MirOp::MovSsRegFromXmm0, reg, 0); }
    void emitComissXmm0FrameSetb(int disp) {
        Mir m;
        m.op   = MirOp::ComissXmm0FrameSetb;
        m.disp = disp;
        push(m);
    }
    void emitComissXmm0MemAtSetb(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComissXmm0MemAtSetb;
        m.imm64 = addr;
        push(m);
    }
    void emitComissXmm0RegSetb(uint8_t reg) { push(MirOp::ComissXmm0RegSetb, reg, 0); }
    void emitComissXmm0FrameSetbe(int disp) {
        Mir m;
        m.op   = MirOp::ComissXmm0FrameSetbe;
        m.disp = disp;
        push(m);
    }
    void emitComissXmm0MemAtSetbe(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComissXmm0MemAtSetbe;
        m.imm64 = addr;
        push(m);
    }
    void emitComissXmm0RegSetbe(uint8_t reg) { push(MirOp::ComissXmm0RegSetbe, reg, 0); }
    void emitComissXmm0FrameSete(int disp) {
        Mir m;
        m.op   = MirOp::ComissXmm0FrameSete;
        m.disp = disp;
        push(m);
    }
    void emitComissXmm0MemAtSete(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComissXmm0MemAtSete;
        m.imm64 = addr;
        push(m);
    }
    void emitComissXmm0RegSete(uint8_t reg) { push(MirOp::ComissXmm0RegSete, reg, 0); }
    void emitComissXmm0FrameSeta(int disp) {
        Mir m;
        m.op   = MirOp::ComissXmm0FrameSeta;
        m.disp = disp;
        push(m);
    }
    void emitComissXmm0MemAtSeta(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComissXmm0MemAtSeta;
        m.imm64 = addr;
        push(m);
    }
    void emitComissXmm0RegSeta(uint8_t reg) { push(MirOp::ComissXmm0RegSeta, reg, 0); }
    void emitComissXmm0FrameSetae(int disp) {
        Mir m;
        m.op   = MirOp::ComissXmm0FrameSetae;
        m.disp = disp;
        push(m);
    }
    void emitComissXmm0MemAtSetae(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComissXmm0MemAtSetae;
        m.imm64 = addr;
        push(m);
    }
    void emitComissXmm0RegSetae(uint8_t reg) { push(MirOp::ComissXmm0RegSetae, reg, 0); }
    void emitComissXmm0FrameSetnz(int disp) {
        Mir m;
        m.op   = MirOp::ComissXmm0FrameSetnz;
        m.disp = disp;
        push(m);
    }
    void emitComissXmm0MemAtSetnz(uint64_t addr) {
        Mir m;
        m.op    = MirOp::ComissXmm0MemAtSetnz;
        m.imm64 = addr;
        push(m);
    }
    void emitComissXmm0RegSetnz(uint8_t reg) { push(MirOp::ComissXmm0RegSetnz, reg, 0); }
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
