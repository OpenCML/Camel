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
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "mir.h"

namespace camel::jit::x64 {

class MirBuilder {
  public:
    explicit MirBuilder(MirBuffer &buf) : buf_(buf) {}

    // 下一条 push 的 MIR 将带上此 pc（用于 pcToOffset 与 debug）
    void setNextPc(uint32_t pc) { nextPc_ = pc; }

    // C++ ABI wrapper is now generated as raw bytes; MIR body uses JIT internal convention.
    // Kept as no-op for backward compatibility with code that still calls it.
    void emitPrologueWin64() {}

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
    void emitJzRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JzRel32;
        m.imm32 = targetPc;
        push(m);
    }
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
    void emitJlRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JlRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitJgRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JgRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitJgeRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JgeRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitJeRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JeRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitJneRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::JneRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitVCmpRegImm(VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpRegImm;
        m.r0    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
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
    void emitCallRax() { push(MirOp::CallRax, 0, 0); }
    void emitNativeJitFuncCall(NativeJitCallParams *params) {
        Mir m;
        m.op    = MirOp::NativeJitFuncCall;
        m.imm64 = reinterpret_cast<uint64_t>(params);
        push(m);
    }
    void emitCallRel32(uint32_t targetPc) {
        Mir m;
        m.op    = MirOp::CallRel32;
        m.imm32 = targetPc;
        push(m);
    }
    void emitPushRdi() { push(MirOp::PushRdi, 0, 0); }
    void emitPopRdi() { push(MirOp::PopRdi, 0, 0); }
    void emitPushRsi() { push(MirOp::PushRsi, 0, 0); }
    void emitPopRsi() { push(MirOp::PopRsi, 0, 0); }
    void emitPushRbx() { push(MirOp::PushRbx, 0, 0); }
    void emitPopRbx() { push(MirOp::PopRbx, 0, 0); }
    void emitSubRsp8() { push(MirOp::SubRsp8, 0, 0); }
    void emitAddRsp8() { push(MirOp::AddRsp8, 0, 0); }
    void emitRet() { push(MirOp::Ret, 0, 0); }
    void emitJmpRax() { push(MirOp::JmpRax, 0, 0); }
    /** Debug 构建下在每两条指令间插入；pc 为当前字节码 pc，供 jitDebugTrace 打印 */
    void emitDebugTrace(uint32_t pc) {
        Mir m;
        m.op    = MirOp::DebugTrace;
        m.pc    = pc;
        m.imm32 = pc;
        buf_.push_back(m);
    }

    // VReg instructions share the same compact encoding:
    // - unary/binary vregs go through r0/r1
    // - the third operand reuses imm32 to avoid growing the Mir struct
    // This keeps MIR dense enough for debug dumps and linear optimization.
    //
    // `disp` on selected ops is intentionally multi-purpose:
    // - frame byte offset for load/store
    // - auxiliary slot location for spill-aware tests such as JOIN
    void emitVLoadFromFrame(VRegId vreg, int disp) {
        Mir m;
        m.op   = MirOp::VLoadFromFrame;
        m.r0   = static_cast<uint8_t>(vreg & 0xff);
        m.disp = disp;
        push(m);
    }
    void emitVStoreToFrame(int disp, VRegId vreg) {
        Mir m;
        m.op   = MirOp::VStoreToFrame;
        m.r0   = static_cast<uint8_t>(vreg & 0xff);
        m.disp = disp;
        push(m);
    }
    void emitVLoadFromMemAt(VRegId vreg, uint64_t addr) {
        Mir m;
        m.op    = MirOp::VLoadFromMemAt;
        m.r0    = static_cast<uint8_t>(vreg & 0xff);
        m.imm64 = addr;
        push(m);
    }
    void emitVCopy(VRegId dst, VRegId src) {
        push(MirOp::VCopy, static_cast<uint8_t>(dst & 0xff), static_cast<uint8_t>(src & 0xff));
    }
    void emitVTest(VRegId vreg, int32_t frameDisp = 0) {
        Mir m;
        m.op = MirOp::VTest;
        m.r0 = static_cast<uint8_t>(vreg & 0xff);
        // frameDisp is optional fallback metadata for the encoder when the
        // tested value may end up spilled and needs to be reloaded from frame.
        m.disp = frameDisp;
        push(m);
    }
    void emitVCmove(VRegId dst, VRegId src) {
        push(MirOp::VCmove, static_cast<uint8_t>(dst & 0xff), static_cast<uint8_t>(src & 0xff));
    }
    void emitVCmovnz(VRegId dst, VRegId src) {
        push(MirOp::VCmovnz, static_cast<uint8_t>(dst & 0xff), static_cast<uint8_t>(src & 0xff));
    }
    void emitVLoadImm32(VRegId vreg, uint32_t imm32) {
        Mir m;
        m.op    = MirOp::VLoadImm32;
        m.r0    = static_cast<uint8_t>(vreg & 0xff);
        m.imm32 = imm32;
        push(m);
    }
    void emitVLoadImm64(VRegId vreg, uint64_t imm64) {
        Mir m;
        m.op    = MirOp::VLoadImm64;
        m.r0    = static_cast<uint8_t>(vreg & 0xff);
        m.imm64 = imm64;
        push(m);
    }
    void emitVMovFromRax(VRegId vreg) {
        Mir m;
        m.op = MirOp::VMovFromRax;
        m.r0 = static_cast<uint8_t>(vreg & 0xff);
        push(m);
    }
    void emitVMovToRax(VRegId vreg) {
        Mir m;
        m.op = MirOp::VMovToRax;
        m.r0 = static_cast<uint8_t>(vreg & 0xff);
        push(m);
    }
    void emitVRet(VRegId vreg) {
        Mir m;
        m.op = MirOp::VRet;
        m.r0 = static_cast<uint8_t>(vreg & 0xff);
        push(m);
    }
    void emitVAdd(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VAdd;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVSub(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VSub;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVMul(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VMul;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVIdiv(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VIdiv;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetL(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetL;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetLE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetLE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetG(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetG;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetGE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetGE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetNE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetNE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }

    // Immediate-operand arithmetic/compare: r0 = r1 op imm32
    void emitVAddImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VAddImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVSubImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VSubImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVCmpSetLImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpSetLImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVCmpSetLEImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpSetLEImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVCmpSetGImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpSetGImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVCmpSetGEImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpSetGEImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVCmpSetEImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpSetEImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }
    void emitVCmpSetNEImm(VRegId dst, VRegId src, int32_t imm) {
        Mir m;
        m.op    = MirOp::VCmpSetNEImm;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(src & 0xff);
        m.imm32 = static_cast<uint32_t>(imm);
        push(m);
    }

    // Double (VXmm*): 槽 8 字节，vreg 存 GPR 中的 double 位模式
    void emitVXmmLoadFromFrame(VRegId vreg, int disp) {
        Mir m;
        m.op   = MirOp::VXmmLoadFromFrame;
        m.r0   = static_cast<uint8_t>(vreg & 0xff);
        m.disp = disp;
        push(m);
    }
    void emitVXmmStoreToFrame(int disp, VRegId vreg) {
        Mir m;
        m.op   = MirOp::VXmmStoreToFrame;
        m.r0   = static_cast<uint8_t>(vreg & 0xff);
        m.disp = disp;
        push(m);
    }
    void emitVXmmLoadFromMemAt(VRegId vreg, uint64_t addr) {
        Mir m;
        m.op    = MirOp::VXmmLoadFromMemAt;
        m.r0    = static_cast<uint8_t>(vreg & 0xff);
        m.imm64 = addr;
        push(m);
    }
    void emitVXmmAdd(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmAdd;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmSub(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmSub;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmMul(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmMul;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmDiv(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmDiv;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmCmpSetB(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmCmpSetB;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmCmpSetBE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmCmpSetBE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmCmpSetE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmCmpSetE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmCmpSetA(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmCmpSetA;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmCmpSetAE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmCmpSetAE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmmCmpSetNZ(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmmCmpSetNZ;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }

    // 32 位整型 (I*)
    void emitVAdd32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VAdd32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVSub32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VSub32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVMul32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VMul32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVIdiv32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VIdiv32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetL32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetL32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetLE32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetLE32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetG32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetG32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetGE32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetGE32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetE32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetE32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVCmpSetNE32(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VCmpSetNE32;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }

    // 32 位浮点 (VXmm32*)
    void emitVXmm32LoadFromFrame(VRegId vreg, int disp) {
        Mir m;
        m.op   = MirOp::VXmm32LoadFromFrame;
        m.r0   = static_cast<uint8_t>(vreg & 0xff);
        m.disp = disp;
        push(m);
    }
    void emitVXmm32StoreToFrame(int disp, VRegId vreg) {
        Mir m;
        m.op   = MirOp::VXmm32StoreToFrame;
        m.r0   = static_cast<uint8_t>(vreg & 0xff);
        m.disp = disp;
        push(m);
    }
    void emitVXmm32LoadFromMemAt(VRegId vreg, uint64_t addr) {
        Mir m;
        m.op    = MirOp::VXmm32LoadFromMemAt;
        m.r0    = static_cast<uint8_t>(vreg & 0xff);
        m.imm64 = addr;
        push(m);
    }
    void emitVXmm32Add(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32Add;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32Sub(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32Sub;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32Mul(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32Mul;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32Div(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32Div;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32CmpSetB(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32CmpSetB;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32CmpSetBE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32CmpSetBE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32CmpSetE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32CmpSetE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32CmpSetA(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32CmpSetA;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32CmpSetAE(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32CmpSetAE;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }
    void emitVXmm32CmpSetNZ(VRegId dst, VRegId left, VRegId right) {
        Mir m;
        m.op    = MirOp::VXmm32CmpSetNZ;
        m.r0    = static_cast<uint8_t>(dst & 0xff);
        m.r1    = static_cast<uint8_t>(left & 0xff);
        m.imm32 = static_cast<uint32_t>(right);
        push(m);
    }

    // Win64: 准备 trampoline(slots, ctx, pc) 并 call rax。优化 pass 可删前两条 mov。
    // trampoline 会调用 fn(callee_slots) 覆盖 rdi，故需 push/pop 保存 caller 的 slot base
    void emitCallTrampolineWin64(uint32_t pc, uint64_t trampolineAddr) {
        emitPushRdi();
        emitMovRegReg(kRegRcx, kRegRdi);
        emitMovRegReg(kRegRdx, kRegRsi);
        emitMovRegImm32(kRegR8, pc);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
        emitPopRdi();
    }
    void emitCallTrampolineOperWin64(uint32_t pc, uint64_t trampolineAddr) {
        emitPushRdi();
        emitMovRegReg(kRegRcx, kRegRdi);
        emitMovRegReg(kRegRdx, kRegRsi);
        emitMovRegImm32(kRegR8, pc);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
        emitPopRdi();
    }

    // SysV：rdi/rsi 已是 slots/ctx，设 rdx=pc、rax=addr 后 call
    // trampoline 会调用 fn(callee_slots) 覆盖 rdi，故需 push/pop 保存 caller 的 slot base
    void emitCallTrampolineSysV(uint32_t pc, uint64_t trampolineAddr) {
        emitPushRdi();
        emitMovRegImm32(kRegRdx, pc);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
        emitPopRdi();
    }
    // SysV：第 3 参 pc→rdx，rax=addr，call（graph 从 slots[0] 即 Frame* 获取）
    void emitCallTrampolineOperSysV(uint32_t pc, uint64_t trampolineAddr) {
        emitPushRdi();
        emitMovRegImm32(kRegRdx, pc);
        emitMovRegImm64(kRegRax, trampolineAddr);
        emitCallRax();
        emitPopRdi();
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
