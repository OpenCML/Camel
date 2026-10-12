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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "mir_encode.h"
#include "../regalloc/regalloc.h"
#include "camel/core/context/frame.h"

#include <climits>
#include <vector>

namespace camel::jit::x64 {


using ::camel::jit::kSpilled;
using ::camel::jit::VRegAllocation;

struct JumpPatch {
    size_t jumpPos;        // Jump instruction start offset
    size_t targetMirIndex; // Target MIR index (uniquely corresponds to startOffset[targetMirIndex])
    int instrLen; // Jump instruction length (used to locate rel32 write position: jmp=1, jz/jle=2)
    size_t asmLineIndex; // Corresponding asm line index; rel display is updated after patching
    size_t jumpEndPos;   // Offset of the "next instruction" after encoding; rel = targetOffset -
                         // jumpEndPos
};

void encodeMirBuffer(
    const MirBuffer &buf, std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset,
    const VRegAllocation *vregAlloc,
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries,
    void *debugTraceFn, size_t faultExitOffset) {
    // Every operand that is used has a register: linearScanVReg fails otherwise. A register of
    // kSpilled here means a value that is defined and never used; its definition is skipped.
    const bool recordAsm = asmOut != nullptr || instructionBoundaries != nullptr;
    Encoder enc(code, asmOut, baseOffset, recordAsm);
    auto pregFor = [vregAlloc](VRegId v) -> int {
        if (!vregAlloc)
            return kSpilled;
        return vregAlloc->pregForVReg(v);
    };
    std::vector<size_t> startOffset(buf.size());
    // pc -> first MIR carrying that bytecode PC. All jump patching is anchored
    // to MIR start offsets rather than instruction estimates so earlier size
    // changes from peephole or allocation cannot desynchronize targets.
    std::unordered_map<size_t, size_t> pcToMirIndex;
    for (size_t i = 0; i < buf.size(); ++i)
        if (buf[i].hasPc() &&
            pcToMirIndex.find(static_cast<size_t>(buf[i].pc)) == pcToMirIndex.end())
            pcToMirIndex[static_cast<size_t>(buf[i].pc)] = i;

    std::vector<JumpPatch> patches;

    // Two-operand lowering of dst = left <op> right. The allocator frees a register at the
    // last use of its value, so dst may be the register of either operand, and both operands
    // may be one register. r11 is never allocated: it is the scratch register of this file.
    auto u8 = [](int r) { return static_cast<uint8_t>(r); };
    // Commutative: `op(a, b)` emits a := a <op> b.
    auto emitCommutative = [&](int dr, int lr, int rr, auto &&op) {
        if (dr == lr) {
            op(u8(dr), u8(rr));
        } else if (dr == rr) {
            op(u8(dr), u8(lr));
        } else {
            enc.emitMovRegReg(u8(dr), u8(lr));
            op(u8(dr), u8(rr));
        }
    };
    // The condition `cc` of flags already set, as 0 / 1 in dst.
    auto emitSetcc = [&](int dr, Encoder::Cond cc) { enc.setccReg(cc, u8(dr)); };
    // Fault sites. The check jumps forward to a stub emitted after the body; the stub records
    // the fault and leaves through the common exit.
    struct FaultStub {
        size_t relPos; // rel32 of the jcc to patch
        uint32_t pc;
        JitFaultKind kind;
    };
    std::vector<FaultStub> faultStubs;
    auto emitFaultIf = [&](Encoder::Cond cc, uint32_t pc, JitFaultKind kind) {
        faultStubs.push_back({enc.jccRel32(cc, 0), pc, kind});
    };
    // Signed division: the quotient of left / right in dst. idiv takes the dividend in
    // rdx:rax and leaves quotient and remainder there, so whatever else lives in the two is
    // saved around it, and the divisor is moved out of their way first. A checked division
    // faults on a zero divisor, and computes x / -1 as -x: idiv raises the hardware's divide
    // error for the most negative x, where the quotient wraps as every other integer overflow.
    auto emitIdiv = [&](int dr, int lr, int rr, bool is32, bool checked, uint32_t pc) {
        if (is32)
            enc.movRegReg32(kRegR11, u8(rr));
        else
            enc.emitMovRegReg(kRegR11, u8(rr));
        size_t skipNegRel = 0, doneRel = 0;
        if (checked) {
            if (is32) {
                enc.testRegReg32(kRegR11);
                emitFaultIf(Encoder::kCondE, pc, JitFaultKind::DivisionByZero);
                enc.cmpReg32Imm8(kRegR11, -1);
            } else {
                enc.testRegReg(kRegR11);
                emitFaultIf(Encoder::kCondE, pc, JitFaultKind::DivisionByZero);
                enc.cmpRegImm32(kRegR11, -1);
            }
            skipNegRel = enc.jccRel32(Encoder::kCondNE, 0);
            if (is32) {
                if (dr != lr)
                    enc.movRegReg32(u8(dr), u8(lr));
                enc.negReg32(u8(dr));
            } else {
                enc.emitMovRegReg(u8(dr), u8(lr));
                enc.negReg(u8(dr));
            }
            doneRel = enc.jmpRel32Patchable();
            enc.patchRel32At(skipNegRel, static_cast<int32_t>(enc.here() - (skipNegRel + 4)));
        }
        if (dr != static_cast<int>(kRegRax))
            enc.pushReg(kRegRax);
        if (dr != static_cast<int>(kRegRdx))
            enc.pushReg(kRegRdx);
        if (lr != static_cast<int>(kRegRax)) {
            if (is32)
                enc.movRegReg32(kRegRax, u8(lr));
            else
                enc.emitMovRegReg(kRegRax, u8(lr));
        }
        if (is32)
            enc.cdqIdivReg32(kRegR11);
        else
            enc.cqoIdivReg(kRegR11);
        if (dr != static_cast<int>(kRegRax)) {
            if (is32)
                enc.movRegReg32(u8(dr), kRegRax);
            else
                enc.emitMovRegReg(u8(dr), kRegRax);
        }
        if (dr != static_cast<int>(kRegRdx))
            enc.popReg(kRegRdx);
        if (dr != static_cast<int>(kRegRax))
            enc.popReg(kRegRax);
        if (checked)
            enc.patchRel32At(doneRel, static_cast<int32_t>(enc.here() - (doneRel + 4)));
    };
    // The interpreters fault on a floating-point divisor that compares equal to zero (+0.0 or
    // -0.0): every bit but the sign is clear.
    auto emitFloatDivisorCheck = [&](int rr, bool isDouble, uint32_t pc) {
        if (isDouble) {
            enc.emitMovRegReg(kRegR11, u8(rr));
            enc.addRegReg(kRegR11, kRegR11);
        } else {
            enc.movRegReg32(kRegR11, u8(rr));
            enc.addRegReg32(kRegR11, kRegR11);
        }
        emitFaultIf(Encoder::kCondE, pc, JitFaultKind::DivisionByZero);
    };
    // Floating-point arithmetic on the bit patterns held in general registers.
    auto emitFloatArith = [&](int dr, int lr, int rr, bool isDouble, uint8_t opcode) {
        if (isDouble) {
            enc.movqXmmFromReg(0, u8(lr));
            enc.movqXmmFromReg(1, u8(rr));
            enc.sseArithXmm0Xmm1(true, opcode);
            enc.movqRegFromXmm(u8(dr), 0);
        } else {
            enc.movdXmmFromReg(0, u8(lr));
            enc.movdXmmFromReg(1, u8(rr));
            enc.sseArithXmm0Xmm1(false, opcode);
            enc.movdRegFromXmm(u8(dr), 0);
        }
    };
    // Floating-point comparison as 0 / 1 in dst, false whenever an operand is a NaN (true for
    // "not equal"), as the comparison operators of the interpreters.
    enum class FloatCmp { Lt, Le, Eq, Gt, Ge, Ne };
    auto emitFloatCmp = [&](int dr, int lr, int rr, bool isDouble, FloatCmp cmp) {
        if (isDouble) {
            enc.movqXmmFromReg(0, u8(lr));
            enc.movqXmmFromReg(1, u8(rr));
        } else {
            enc.movdXmmFromReg(0, u8(lr));
            enc.movdXmmFromReg(1, u8(rr));
        }
        switch (cmp) {
        // "above" needs CF = 0 and ZF = 0, "above or equal" CF = 0: both false on a NaN. The
        // "less" comparisons are the "greater" ones with the operands exchanged.
        case FloatCmp::Gt:
            enc.ucomiXmm(isDouble, 0, 1);
            enc.setccReg(Encoder::kCondA, u8(dr));
            break;
        case FloatCmp::Ge:
            enc.ucomiXmm(isDouble, 0, 1);
            enc.setccReg(Encoder::kCondAE, u8(dr));
            break;
        case FloatCmp::Lt:
            enc.ucomiXmm(isDouble, 1, 0);
            enc.setccReg(Encoder::kCondA, u8(dr));
            break;
        case FloatCmp::Le:
            enc.ucomiXmm(isDouble, 1, 0);
            enc.setccReg(Encoder::kCondAE, u8(dr));
            break;
        // Equal: ZF = 1 and no NaN (PF = 0). Not equal: ZF = 0 or a NaN (PF = 1).
        case FloatCmp::Eq:
            enc.ucomiXmm(isDouble, 0, 1);
            enc.setccReg8(Encoder::kCondNP, kRegR11);
            enc.setccReg8(Encoder::kCondE, u8(dr));
            enc.andReg8Reg8(u8(dr), kRegR11);
            enc.movzxRegReg8(u8(dr));
            break;
        case FloatCmp::Ne:
            enc.ucomiXmm(isDouble, 0, 1);
            enc.setccReg8(Encoder::kCondP, kRegR11);
            enc.setccReg8(Encoder::kCondNE, u8(dr));
            enc.orReg8Reg8(u8(dr), kRegR11);
            enc.movzxRegReg8(u8(dr));
            break;
        }
    };
    for (size_t i = 0; i < buf.size(); ++i) {
        const Mir &m   = buf[i];
        startOffset[i] = enc.here();
        switch (m.op) {
        case MirOp::VLoadFromFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movRegFromFrame(static_cast<uint8_t>(r), m.disp);
            break;
        }
        case MirOp::VStoreToFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movToFrame(m.disp, static_cast<uint8_t>(r));
            break;
        }
        case MirOp::VLoadFromMemAt: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movRegFromMemAt(static_cast<uint8_t>(r), m.imm64);
            break;
        }
        case MirOp::VCopy: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int sr = pregFor(static_cast<VRegId>(m.r1));
            if (dr >= 0 && sr >= 0)
                enc.emitMovRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(sr));
            break;
        }
        case MirOp::VTest: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.testRegReg(static_cast<uint8_t>(r));
            break;
        }
        case MirOp::VCmove: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int sr = pregFor(static_cast<VRegId>(m.r1));
            if (dr >= 0 && sr >= 0)
                enc.cmoveRegFromReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(sr));
            break;
        }
        case MirOp::VCmovnz: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int sr = pregFor(static_cast<VRegId>(m.r1));
            if (dr >= 0 && sr >= 0)
                enc.cmovnzRegFromReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(sr));
            break;
        }
        case MirOp::VMovFromRax:
        case MirOp::VMovToRax:
            // Not used on JOIN and similar paths yet; encode as mov reg, rax / mov rax, reg.
            if (vregAlloc) {
                int r = pregFor(static_cast<VRegId>(m.r0));
                if (r >= 0) {
                    if (m.op == MirOp::VMovFromRax)
                        enc.emitMovRegReg(static_cast<uint8_t>(r), kRegRax);
                    else
                        enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(r));
                }
            }
            break;
        case MirOp::MovRegReg:
            enc.emitMovRegReg(static_cast<uint8_t>(m.r0), static_cast<uint8_t>(m.r1));
            break;
        case MirOp::MovRegImm32:
            enc.emitMovRegImm32(static_cast<uint8_t>(m.r0), m.imm32);
            break;
        case MirOp::MovRegImm64:
            enc.emitMovRegImm64(static_cast<uint8_t>(m.r0), m.imm64);
            break;
        case MirOp::VLoadImm32: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.emitMovRegImm32(static_cast<uint8_t>(r), m.imm32);
            break;
        }
        case MirOp::VLoadImm64: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.emitMovRegImm64(static_cast<uint8_t>(r), m.imm64);
            break;
        }
        case MirOp::VRet: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(r));
                enc.ret();
            }
            break;
        }
        case MirOp::VAdd: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (dr == lr) {
                    enc.addRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(rr));
                } else if (dr == rr) {
                    enc.addRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(lr));
                } else {
                    enc.emitMovRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(lr));
                    enc.addRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(rr));
                }
            }
            break;
        }
        case MirOp::VSub: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (dr == lr) {
                    enc.subRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(rr));
                } else if (dr != rr) {
                    enc.emitMovRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(lr));
                    enc.subRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(rr));
                } else {
                    enc.negReg(static_cast<uint8_t>(dr));
                    enc.addRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(lr));
                }
            }
            break;
        }
        case MirOp::VMul: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0)
                emitCommutative(dr, lr, rr, [&](uint8_t a, uint8_t b) { enc.imulRegReg(a, b); });
            break;
        }
        case MirOp::VIdiv: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0)
                emitIdiv(dr, lr, rr, false, (m.imm64 & 1) != 0, static_cast<uint32_t>(m.imm64 >> 1));
            break;
        }
        case MirOp::VCmpSetL:
        case MirOp::VCmpSetLE:
        case MirOp::VCmpSetG:
        case MirOp::VCmpSetGE:
        case MirOp::VCmpSetE:
        case MirOp::VCmpSetNE: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.cmpRegReg(static_cast<uint8_t>(lr), static_cast<uint8_t>(rr));
                emitSetcc(
                    dr,
                    m.op == MirOp::VCmpSetL    ? Encoder::kCondL
                    : m.op == MirOp::VCmpSetLE ? Encoder::kCondLE
                    : m.op == MirOp::VCmpSetG  ? Encoder::kCondG
                    : m.op == MirOp::VCmpSetGE ? Encoder::kCondGE
                    : m.op == MirOp::VCmpSetE  ? Encoder::kCondE
                                               : Encoder::kCondNE);
            }
            break;
        }
        case MirOp::VAddImm:
        case MirOp::VSubImm: {
            int dr      = pregFor(static_cast<VRegId>(m.r0));
            int sr      = pregFor(static_cast<VRegId>(m.r1));
            int32_t imm = static_cast<int32_t>(m.imm32);
            if (dr >= 0 && sr >= 0) {
                if (dr != sr)
                    enc.emitMovRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(sr));
                if (m.op == MirOp::VAddImm)
                    enc.addRegImm32(static_cast<uint8_t>(dr), imm);
                else
                    enc.subRegImm32(static_cast<uint8_t>(dr), imm);
            }
            break;
        }
        case MirOp::VCmpSetLImm:
        case MirOp::VCmpSetLEImm:
        case MirOp::VCmpSetGImm:
        case MirOp::VCmpSetGEImm:
        case MirOp::VCmpSetEImm:
        case MirOp::VCmpSetNEImm: {
            int dr      = pregFor(static_cast<VRegId>(m.r0));
            int sr      = pregFor(static_cast<VRegId>(m.r1));
            int32_t imm = static_cast<int32_t>(m.imm32);
            if (dr >= 0 && sr >= 0) {
                enc.cmpRegImm32(static_cast<uint8_t>(sr), imm);
                emitSetcc(
                    dr,
                    m.op == MirOp::VCmpSetLImm    ? Encoder::kCondL
                    : m.op == MirOp::VCmpSetLEImm ? Encoder::kCondLE
                    : m.op == MirOp::VCmpSetGImm  ? Encoder::kCondG
                    : m.op == MirOp::VCmpSetGEImm ? Encoder::kCondGE
                    : m.op == MirOp::VCmpSetEImm  ? Encoder::kCondE
                                                  : Encoder::kCondNE);
            }
            break;
        }
        case MirOp::JzRel32: {
            size_t ti = buf.size();
            auto it   = pcToMirIndex.find(static_cast<size_t>(m.imm32));
            if (it != pcToMirIndex.end())
                ti = it->second;
            // Emit a placeholder now and patch the rel32 once all MIR offsets
            // are known. This keeps the main encoding loop single-pass.
            patches.push_back({enc.here(), ti, 6, 0, 0});
            enc.jzRel32(0);
            patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
            patches.back().jumpEndPos   = enc.here();
            break;
        }
        case MirOp::JmpRel32: {
            size_t ti = buf.size();
            auto it   = pcToMirIndex.find(static_cast<size_t>(m.imm32));
            if (it != pcToMirIndex.end())
                ti = it->second;
            patches.push_back({enc.here(), ti, 5, 0, 0});
            enc.jmpRel32(0);
            patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
            patches.back().jumpEndPos   = enc.here();
            break;
        }
        case MirOp::JleRel32:
        case MirOp::JlRel32:
        case MirOp::JgRel32:
        case MirOp::JgeRel32:
        case MirOp::JeRel32:
        case MirOp::JneRel32: {
            size_t ti = buf.size();
            auto it   = pcToMirIndex.find(static_cast<size_t>(m.imm32));
            if (it != pcToMirIndex.end())
                ti = it->second;
            patches.push_back({enc.here(), ti, 6, 0, 0});
            switch (m.op) {
            case MirOp::JleRel32:
                enc.jleRel32(0);
                break;
            case MirOp::JlRel32:
                enc.jlRel32(0);
                break;
            case MirOp::JgRel32:
                enc.jgRel32(0);
                break;
            case MirOp::JgeRel32:
                enc.jgeRel32(0);
                break;
            case MirOp::JeRel32:
                enc.jeRel32(0);
                break;
            case MirOp::JneRel32:
                enc.jneRel32(0);
                break;
            default:
                break;
            }
            patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
            patches.back().jumpEndPos   = enc.here();
            break;
        }
        case MirOp::VCmpRegImm: {
            int sr = pregFor(static_cast<VRegId>(m.r0));
            // This MIR exists purely to feed a following Jcc. It intentionally
            // leaves the result in flags instead of materializing a boolean.
            if (sr >= 0)
                enc.cmpRegImm32(static_cast<uint8_t>(sr), static_cast<int32_t>(m.imm32));
            break;
        }
        case MirOp::JmpRel8:
            enc.jmpRel8(static_cast<int8_t>(m.disp & 0xff));
            break;
        case MirOp::JleRel8:
            enc.jleRel8(static_cast<int8_t>(m.disp & 0xff));
            break;
        case MirOp::Ret:
            enc.ret();
            break;
        case MirOp::JmpRax:
            enc.jmpRax();
            break;
        case MirOp::PushRdi:
            enc.pushRdi();
            break;
        case MirOp::PopRdi:
            enc.popRdi();
            break;
        case MirOp::PushRsi:
            enc.pushRsi();
            break;
        case MirOp::PopRsi:
            enc.popRsi();
            break;
        case MirOp::PushRbx:
            enc.pushRbx();
            break;
        case MirOp::PopRbx:
            enc.popRbx();
            break;
        case MirOp::SubRsp8:
            enc.subRsp8();
            break;
        case MirOp::AddRsp8:
            enc.addRsp8();
            break;
        case MirOp::CallRax:
            enc.callRax();
            break;
        case MirOp::CallRel32: {
            size_t ti = buf.size();
            auto it   = pcToMirIndex.find(static_cast<size_t>(m.imm32));
            if (it != pcToMirIndex.end())
                ti = it->second;
            patches.push_back({enc.here(), ti, 5, 0, 0});
            enc.callRel32(0);
            patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
            patches.back().jumpEndPos   = enc.here();
            break;
        }
        case MirOp::DebugTrace:
            if (debugTraceFn)
                enc.emitDebugTraceCall(m.pc, debugTraceFn);
            break;
        case MirOp::MovRsiR13:
#if defined(_WIN32) || defined(_WIN64)
            enc.nop();
#else
            // r13 is seeded once by the SysV entry wrapper and survives helper calls.
            enc.emitBytes({0x4C, 0x89, 0xEE});
            enc.asmLine("mov rsi, r13  ; restore jitCtx");
#endif
            break;
        case MirOp::NativeJitFuncCall: {
            auto *p = reinterpret_cast<const NativeJitCallParams *>(m.imm64);

            // The callee's frame is built below the saved slot base, with rdi still on the
            // caller's frame; arguments already in registers are not disturbed (r11 is not
            // allocatable).
            enc.pushRdi();
            enc.subRspImm(p->calleeSlotBytes);
            // slot[0] is the Frame* by convention; the callee inherits the caller's.
            enc.movRegFromFrame(kRegR11, 0);
            enc.movToStack(0, kRegR11);
            // Direct-call arguments land on the callee's formal port slots, not on sequential
            // slot numbers.
            for (const auto &arg : p->args) {
                int reg = arg.vreg != kInvalidVReg ? pregFor(arg.vreg) : -1;
                if (reg < 0) {
                    if (arg.isImm)
                        enc.emitMovRegImm64(kRegR11, arg.imm);
                    else
                        enc.movRegFromFrame(kRegR11, arg.srcDisp);
                    reg = kRegR11;
                }
                enc.movToStack(arg.dstDisp, static_cast<uint8_t>(reg));
            }
            enc.movRdiRsp();
#if !defined(_WIN32) && !defined(_WIN64)
            enc.emitBytes({0x4C, 0x89, 0xEE});
            enc.asmLine("mov rsi, r13  ; jitCtx");
#endif
            // Recorded after the mov above: the patch rewrites the rel32 of the call at this
            // offset. MIR index 0 is the body's entry.
            patches.push_back({enc.here(), 0, 5, 0, 0});
            enc.callRel32(0);
            patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
            patches.back().jumpEndPos   = enc.here();

            enc.addRspImm(p->calleeSlotBytes);
            enc.popRdi();
            if (p->resultVReg != kInvalidVReg) {
                // Result-visible mode: keep the call result in a vreg so a
                // following VStore/VLoad pair can be optimized away upstream.
                int preg = pregFor(p->resultVReg);
                if (preg >= 0)
                    enc.emitMovRegReg(static_cast<uint8_t>(preg), kRegRax);
            } else {
                enc.movToFrame(p->resultDisp, kRegRax);
            }
            break;
        }
        case MirOp::PushFrameSlot:
            enc.pushFrameSlot(m.disp);
            break;
        case MirOp::PushImm64:
            enc.emitMovRegImm64(kRegR11, m.imm64);
            enc.pushReg(kRegR11);
            break;
        case MirOp::PopFrameSlot:
            enc.popFrameSlot(m.disp);
            break;
        case MirOp::Nop:
            enc.nop();
            break;
        // A double in a slot is its 64-bit pattern: loading and storing it is a plain move.
        case MirOp::VXmmLoadFromFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movRegFromFrame(static_cast<uint8_t>(r), m.disp);
            break;
        }
        case MirOp::VXmmStoreToFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movToFrame(m.disp, static_cast<uint8_t>(r));
            break;
        }
        case MirOp::VXmmLoadFromMemAt: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movRegFromMemAt(static_cast<uint8_t>(r), m.imm64);
            break;
        }
        case MirOp::VXmmAdd:
        case MirOp::VXmmSub:
        case MirOp::VXmmMul:
        case MirOp::VXmmDiv:
        case MirOp::VXmm32Add:
        case MirOp::VXmm32Sub:
        case MirOp::VXmm32Mul:
        case MirOp::VXmm32Div: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                const bool isDouble = m.op == MirOp::VXmmAdd || m.op == MirOp::VXmmSub ||
                                      m.op == MirOp::VXmmMul || m.op == MirOp::VXmmDiv;
                const uint8_t opcode =
                    (m.op == MirOp::VXmmAdd || m.op == MirOp::VXmm32Add)   ? 0x58
                    : (m.op == MirOp::VXmmSub || m.op == MirOp::VXmm32Sub) ? 0x5c
                    : (m.op == MirOp::VXmmMul || m.op == MirOp::VXmm32Mul) ? 0x59
                                                                           : 0x5e;
                if (opcode == 0x5e && (m.imm64 & 1) != 0)
                    emitFloatDivisorCheck(rr, isDouble, static_cast<uint32_t>(m.imm64 >> 1));
                emitFloatArith(dr, lr, rr, isDouble, opcode);
            }
            break;
        }
        case MirOp::VXmmCmpSetB:
        case MirOp::VXmmCmpSetBE:
        case MirOp::VXmmCmpSetE:
        case MirOp::VXmmCmpSetA:
        case MirOp::VXmmCmpSetAE:
        case MirOp::VXmmCmpSetNZ:
        case MirOp::VXmm32CmpSetB:
        case MirOp::VXmm32CmpSetBE:
        case MirOp::VXmm32CmpSetE:
        case MirOp::VXmm32CmpSetA:
        case MirOp::VXmm32CmpSetAE:
        case MirOp::VXmm32CmpSetNZ: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                bool isDouble = true;
                FloatCmp cmp  = FloatCmp::Lt;
                switch (m.op) {
                case MirOp::VXmm32CmpSetB:
                    isDouble = false;
                    [[fallthrough]];
                case MirOp::VXmmCmpSetB:
                    cmp = FloatCmp::Lt;
                    break;
                case MirOp::VXmm32CmpSetBE:
                    isDouble = false;
                    [[fallthrough]];
                case MirOp::VXmmCmpSetBE:
                    cmp = FloatCmp::Le;
                    break;
                case MirOp::VXmm32CmpSetE:
                    isDouble = false;
                    [[fallthrough]];
                case MirOp::VXmmCmpSetE:
                    cmp = FloatCmp::Eq;
                    break;
                case MirOp::VXmm32CmpSetA:
                    isDouble = false;
                    [[fallthrough]];
                case MirOp::VXmmCmpSetA:
                    cmp = FloatCmp::Gt;
                    break;
                case MirOp::VXmm32CmpSetAE:
                    isDouble = false;
                    [[fallthrough]];
                case MirOp::VXmmCmpSetAE:
                    cmp = FloatCmp::Ge;
                    break;
                case MirOp::VXmm32CmpSetNZ:
                    isDouble = false;
                    [[fallthrough]];
                default:
                    cmp = FloatCmp::Ne;
                    break;
                }
                emitFloatCmp(dr, lr, rr, isDouble, cmp);
            }
            break;
        }
        case MirOp::VAdd32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (dr == lr) {
                    enc.addRegReg32(u8(dr), u8(rr));
                } else if (dr == rr) {
                    enc.addRegReg32(u8(dr), u8(lr));
                } else {
                    enc.movRegReg32(u8(dr), u8(lr));
                    enc.addRegReg32(u8(dr), u8(rr));
                }
            }
            break;
        }
        case MirOp::VSub32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (dr == lr) {
                    enc.subRegReg32(u8(dr), u8(rr));
                } else if (dr != rr) {
                    enc.movRegReg32(u8(dr), u8(lr));
                    enc.subRegReg32(u8(dr), u8(rr));
                } else {
                    enc.negReg32(u8(dr));
                    enc.addRegReg32(u8(dr), u8(lr));
                }
            }
            break;
        }
        case MirOp::VMul32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (dr == lr) {
                    enc.imulRegReg32(u8(dr), u8(rr));
                } else if (dr == rr) {
                    enc.imulRegReg32(u8(dr), u8(lr));
                } else {
                    enc.movRegReg32(u8(dr), u8(lr));
                    enc.imulRegReg32(u8(dr), u8(rr));
                }
            }
            break;
        }
        case MirOp::VIdiv32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0)
                emitIdiv(dr, lr, rr, true, (m.imm64 & 1) != 0, static_cast<uint32_t>(m.imm64 >> 1));
            break;
        }
        case MirOp::VCmpSetL32:
        case MirOp::VCmpSetLE32:
        case MirOp::VCmpSetG32:
        case MirOp::VCmpSetGE32:
        case MirOp::VCmpSetE32:
        case MirOp::VCmpSetNE32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.cmpRegReg32(u8(lr), u8(rr));
                emitSetcc(
                    dr,
                    m.op == MirOp::VCmpSetL32    ? Encoder::kCondL
                    : m.op == MirOp::VCmpSetLE32 ? Encoder::kCondLE
                    : m.op == MirOp::VCmpSetG32  ? Encoder::kCondG
                    : m.op == MirOp::VCmpSetGE32 ? Encoder::kCondGE
                    : m.op == MirOp::VCmpSetE32  ? Encoder::kCondE
                                                 : Encoder::kCondNE);
            }
            break;
        }
        // A float in a slot is its 32-bit pattern in the low half, the upper half clear.
        case MirOp::VXmm32LoadFromFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movReg32FromFrame(static_cast<uint8_t>(r), m.disp);
            break;
        }
        case MirOp::VXmm32StoreToFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0)
                enc.movToFrame(m.disp, static_cast<uint8_t>(r));
            break;
        }
        case MirOp::VXmm32LoadFromMemAt: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movRegFromMemAt(static_cast<uint8_t>(r), m.imm64);
                enc.movRegReg32(static_cast<uint8_t>(r), static_cast<uint8_t>(r));
            }
            break;
        }
        }
    }
    // Fault stubs and the exit they share.
    if (!faultStubs.empty()) {
        std::vector<size_t> exitRels;
        for (const FaultStub &stub : faultStubs) {
            enc.patchRel32At(stub.relPos, static_cast<int32_t>(enc.here() - (stub.relPos + 4)));
            enc.movCtxFieldImm32(
                static_cast<int8_t>(kJitCtxFaultKindOffset),
                static_cast<uint32_t>(stub.kind));
            enc.movCtxFieldImm32(static_cast<int8_t>(kJitCtxFaultPcOffset), stub.pc);
            exitRels.push_back(enc.jmpRel32Patchable());
        }
        for (const size_t rel : exitRels)
            enc.patchRel32At(rel, static_cast<int32_t>(enc.here() - (rel + 4)));
        // Whatever the activations between the wrapper and the fault pushed is dropped at once:
        // they are frames of this function only, with nothing to release.
        enc.movRspR14();
        const size_t rel = enc.jmpRel32Patchable();
        enc.patchRel32At(
            rel,
            static_cast<int32_t>(static_cast<int64_t>(faultExitOffset) - static_cast<int64_t>(rel + 4)));
    }
    // Patch rel32: target = startOffset[targetMirIndex], nextIp = jumpPos + instrLen (x86: rel32
    // is relative to the next byte after the instruction).
    for (const auto &p : patches) {
        if (p.targetMirIndex >= buf.size())
            continue;
        size_t targetOffset = startOffset[p.targetMirIndex];
        size_t nextIp       = p.jumpPos + static_cast<size_t>(p.instrLen);
        int32_t rel         = static_cast<int32_t>(targetOffset) - static_cast<int32_t>(nextIp);
        size_t patchAt      = p.jumpPos + (p.instrLen == 5 ? 1u : 2u);
        if (patchAt + 4 <= code.size()) {
            code[patchAt]     = static_cast<uint8_t>(rel & 0xff);
            code[patchAt + 1] = static_cast<uint8_t>((rel >> 8) & 0xff);
            code[patchAt + 2] = static_cast<uint8_t>((rel >> 16) & 0xff);
            code[patchAt + 3] = static_cast<uint8_t>((rel >> 24) & 0xff);
        }
        enc.setAsmLineRel(p.asmLineIndex, rel);
    }
    enc.flushAsmTo(asmOut);
    if (instructionBoundaries) {
        instructionBoundaries->clear();
        const auto &lines     = enc.getAsmLines();
        const size_t codeSize = code.size();
        for (size_t i = 0; i < lines.size(); ++i) {
            size_t start = lines[i].first - baseOffset;
            size_t end   = (i + 1 < lines.size()) ? (lines[i + 1].first - baseOffset) : codeSize;
            size_t len   = (end > start) ? (end - start) : 0;
            instructionBoundaries->emplace_back(start, len, lines[i].second);
        }
    }
}

} // namespace camel::jit::x64
