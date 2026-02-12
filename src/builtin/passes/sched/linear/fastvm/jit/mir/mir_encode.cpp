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
 * Updated: Feb. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_encode.h"
#include "builtin/passes/sched/linear/fastvm/jit/regalloc/regalloc.h"

#include <vector>

namespace camel::jit::x64 {

struct JumpPatch {
    size_t jumpPos;        // 跳转指令起始偏移
    size_t targetMirIndex; // 目标 MIR 下标（唯一对应 startOffset[targetMirIndex]）
    int instrLen;          // 跳转指令长度（用于定位 rel32 写入位置：jmp=1, jz/jle=2）
    size_t asmLineIndex;   // 对应 asm 行下标，修补后更新 rel 显示
    size_t jumpEndPos;     // 编码后“下一条指令”偏移，rel = targetOffset - jumpEndPos
};

void encodeMirBuffer(
    const MirBuffer &buf, std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset,
    const ::camel::jit::VRegAllocation *vregAlloc,
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries,
    void *debugTraceFn) {
    Encoder enc(code, asmOut, baseOffset);
    auto pregFor = [vregAlloc](VRegId v) -> int {
        if (!vregAlloc)
            return ::camel::jit::kSpilled;
        return vregAlloc->pregForVReg(v);
    };
    std::vector<size_t> startOffset(buf.size());
    // pc -> 首个带该 pc 的 MIR 下标，修补时用 startOffset[targetMirIndex] 作为唯一事实来源
    std::unordered_map<size_t, size_t> pcToMirIndex;
    for (size_t i = 0; i < buf.size(); ++i)
        if (buf[i].hasPc() &&
            pcToMirIndex.find(static_cast<size_t>(buf[i].pc)) == pcToMirIndex.end())
            pcToMirIndex[static_cast<size_t>(buf[i].pc)] = i;

    std::vector<JumpPatch> patches;
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
            // 暂未在 JOIN 等路径使用，编码为 mov reg, rax / mov rax, reg
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
            enc.emitMovRegReg(m.r0, m.r1);
            break;
        case MirOp::MovRegImm32:
            enc.emitMovRegImm32(m.r0, m.imm32);
            break;
        case MirOp::MovRegImm64:
            enc.emitMovRegImm64(m.r0, m.imm64);
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
                if (r != static_cast<int>(kRegRax))
                    enc.movRaxFromReg(static_cast<uint8_t>(r));
                enc.ret();
            }
            break;
        }
        case MirOp::VAdd: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (dr != static_cast<int>(kRegRax))
                    enc.movRaxFromReg(static_cast<uint8_t>(lr));
                else
                    enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(lr));
                enc.addRaxFromReg(static_cast<uint8_t>(rr));
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VSub: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movRaxFromReg(static_cast<uint8_t>(lr));
                else
                    enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(lr));
                enc.subRaxFromReg(static_cast<uint8_t>(rr));
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VMul: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movRaxFromReg(static_cast<uint8_t>(lr));
                else
                    enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(lr));
                enc.mulRaxFromReg(static_cast<uint8_t>(rr));
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VIdiv: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movRaxFromReg(static_cast<uint8_t>(lr));
                else
                    enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(lr));
                enc.idivRaxByReg(static_cast<uint8_t>(rr));
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
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
                if (lr != static_cast<int>(kRegRax))
                    enc.movRaxFromReg(static_cast<uint8_t>(lr));
                else
                    enc.emitMovRegReg(kRegRax, static_cast<uint8_t>(lr));
                enc.cmpRaxWithReg(static_cast<uint8_t>(rr));
                switch (m.op) {
                case MirOp::VCmpSetL:
                    enc.setlAlMovzxRax();
                    break;
                case MirOp::VCmpSetLE:
                    enc.setleAlMovzxRax();
                    break;
                case MirOp::VCmpSetG:
                    enc.setgAlMovzxRax();
                    break;
                case MirOp::VCmpSetGE:
                    enc.setgeAlMovzxRax();
                    break;
                case MirOp::VCmpSetE:
                    enc.setzAlMovzxRax();
                    break;
                case MirOp::VCmpSetNE:
                    enc.setnzAlMovzxRax();
                    break;
                default:
                    break;
                }
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::JzRel32: {
            size_t ti = buf.size();
            auto it   = pcToMirIndex.find(static_cast<size_t>(m.imm32));
            if (it != pcToMirIndex.end())
                ti = it->second;
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
        case MirOp::JleRel32: {
            size_t ti = buf.size();
            auto it   = pcToMirIndex.find(static_cast<size_t>(m.imm32));
            if (it != pcToMirIndex.end())
                ti = it->second;
            patches.push_back({enc.here(), ti, 6, 0, 0});
            enc.jleRel32(0);
            patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
            patches.back().jumpEndPos   = enc.here();
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
        case MirOp::PushRdi:
            enc.pushRdi();
            break;
        case MirOp::PopRdi:
            enc.popRdi();
            break;
        case MirOp::CallRax:
            enc.callRax();
            break;
        case MirOp::DebugTrace:
            if (debugTraceFn)
                enc.emitDebugTraceCall(m.pc, debugTraceFn);
            break;
        case MirOp::Nop:
            enc.nop();
            break;
        case MirOp::VXmmLoadFromFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movXmm0FromFrame(m.disp);
                enc.movRegFromXmm0(static_cast<uint8_t>(r));
            }
            break;
        }
        case MirOp::VXmmStoreToFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movXmm0FromReg(static_cast<uint8_t>(r));
                enc.movFrameFromXmm0(m.disp);
            }
            break;
        }
        case MirOp::VXmmLoadFromMemAt: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movXmm0FromMemAt(m.imm64);
                enc.movRegFromXmm0(static_cast<uint8_t>(r));
            }
            break;
        }
        case MirOp::VXmmAdd: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movXmm0FromReg(static_cast<uint8_t>(lr));
                enc.addXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmmSub: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movXmm0FromReg(static_cast<uint8_t>(lr));
                enc.subXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmmMul: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movXmm0FromReg(static_cast<uint8_t>(lr));
                enc.mulXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmmDiv: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movXmm0FromReg(static_cast<uint8_t>(lr));
                enc.divXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmmCmpSetB:
        case MirOp::VXmmCmpSetBE:
        case MirOp::VXmmCmpSetE:
        case MirOp::VXmmCmpSetA:
        case MirOp::VXmmCmpSetAE:
        case MirOp::VXmmCmpSetNZ: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movXmm0FromReg(static_cast<uint8_t>(lr));
                enc.comisdXmm0WithReg(static_cast<uint8_t>(rr));
                switch (m.op) {
                case MirOp::VXmmCmpSetB:
                    enc.setbAlMovzxRax();
                    break;
                case MirOp::VXmmCmpSetBE:
                    enc.setbeAlMovzxRax();
                    break;
                case MirOp::VXmmCmpSetE:
                    enc.setzAlMovzxRax();
                    break;
                case MirOp::VXmmCmpSetA:
                    enc.setaAlMovzxRax();
                    break;
                case MirOp::VXmmCmpSetAE:
                    enc.setaeAlMovzxRax();
                    break;
                case MirOp::VXmmCmpSetNZ:
                    enc.setnzAlMovzxRax();
                    break;
                default:
                    break;
                }
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VAdd32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movEaxFromReg(static_cast<uint8_t>(lr));
                enc.addEaxFromReg(static_cast<uint8_t>(rr));
                enc.movRegFromEax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VSub32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movEaxFromReg(static_cast<uint8_t>(lr));
                enc.subEaxFromReg(static_cast<uint8_t>(rr));
                enc.movRegFromEax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VMul32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movEaxFromReg(static_cast<uint8_t>(lr));
                enc.mulEaxFromReg(static_cast<uint8_t>(rr));
                enc.movRegFromEax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VIdiv32: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                if (lr != static_cast<int>(kRegRax))
                    enc.movEaxFromReg(static_cast<uint8_t>(lr));
                enc.idivEaxByReg(static_cast<uint8_t>(rr));
                enc.movRegFromEax(static_cast<uint8_t>(dr));
            }
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
                if (lr != static_cast<int>(kRegRax))
                    enc.movEaxFromReg(static_cast<uint8_t>(lr));
                enc.cmpEaxWithReg(static_cast<uint8_t>(rr));
                switch (m.op) {
                case MirOp::VCmpSetL32:
                    enc.setlAlMovzxRax();
                    break;
                case MirOp::VCmpSetLE32:
                    enc.setleAlMovzxRax();
                    break;
                case MirOp::VCmpSetG32:
                    enc.setgAlMovzxRax();
                    break;
                case MirOp::VCmpSetGE32:
                    enc.setgeAlMovzxRax();
                    break;
                case MirOp::VCmpSetE32:
                    enc.setzAlMovzxRax();
                    break;
                case MirOp::VCmpSetNE32:
                    enc.setnzAlMovzxRax();
                    break;
                default:
                    break;
                }
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmm32LoadFromFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movSsXmm0FromFrame(m.disp);
                enc.movSsRegFromXmm0(static_cast<uint8_t>(r));
            }
            break;
        }
        case MirOp::VXmm32StoreToFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movSsXmm0FromReg(static_cast<uint8_t>(r));
                enc.movSsFrameFromXmm0(m.disp);
            }
            break;
        }
        case MirOp::VXmm32LoadFromMemAt: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movSsXmm0FromMemAt(m.imm64);
                enc.movSsRegFromXmm0(static_cast<uint8_t>(r));
            }
            break;
        }
        case MirOp::VXmm32Add: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movSsXmm0FromReg(static_cast<uint8_t>(lr));
                enc.addSsXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movSsRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmm32Sub: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movSsXmm0FromReg(static_cast<uint8_t>(lr));
                enc.subSsXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movSsRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmm32Mul: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movSsXmm0FromReg(static_cast<uint8_t>(lr));
                enc.mulSsXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movSsRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
        case MirOp::VXmm32Div: {
            int dr = pregFor(static_cast<VRegId>(m.r0));
            int lr = pregFor(static_cast<VRegId>(m.r1));
            int rr = pregFor(static_cast<VRegId>(m.imm32));
            if (dr >= 0 && lr >= 0 && rr >= 0) {
                enc.movSsXmm0FromReg(static_cast<uint8_t>(lr));
                enc.divSsXmm0FromReg(static_cast<uint8_t>(rr));
                enc.movSsRegFromXmm0(static_cast<uint8_t>(dr));
            }
            break;
        }
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
                enc.movSsXmm0FromReg(static_cast<uint8_t>(lr));
                enc.comissXmm0WithReg(static_cast<uint8_t>(rr));
                switch (m.op) {
                case MirOp::VXmm32CmpSetB:
                    enc.setbAlMovzxRax();
                    break;
                case MirOp::VXmm32CmpSetBE:
                    enc.setbeAlMovzxRax();
                    break;
                case MirOp::VXmm32CmpSetE:
                    enc.setzAlMovzxRax();
                    break;
                case MirOp::VXmm32CmpSetA:
                    enc.setaAlMovzxRax();
                    break;
                case MirOp::VXmm32CmpSetAE:
                    enc.setaeAlMovzxRax();
                    break;
                case MirOp::VXmm32CmpSetNZ:
                    enc.setnzAlMovzxRax();
                    break;
                default:
                    break;
                }
                if (dr != static_cast<int>(kRegRax))
                    enc.movRegFromRax(static_cast<uint8_t>(dr));
            }
            break;
        }
        }
    }
    // 修补 rel32：目标 = startOffset[targetMirIndex]，nextIp = jumpPos + instrLen（x86：rel32
    // 相对指令结束后的下一字节）
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
