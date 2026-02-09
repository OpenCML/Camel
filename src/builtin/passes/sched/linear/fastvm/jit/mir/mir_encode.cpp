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

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_encode.h"
#include "builtin/passes/sched/linear/fastvm/jit/regalloc/regalloc.h"

namespace camel::jit::x64 {

void encodeMirBuffer(
    const MirBuffer &buf, const std::unordered_map<size_t, size_t> &pcToOffset,
    std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset,
    const ::camel::jit::VRegAllocation *vregAlloc) {
    Encoder enc(code, asmOut, baseOffset);
    auto pregFor = [vregAlloc](VRegId v) -> int {
        if (!vregAlloc)
            return ::camel::jit::kSpilled;
        return vregAlloc->pregForVReg(v);
    };
    for (const Mir &m : buf) {
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
            if (r >= 0) {
                enc.movRaxFromMemAt(m.imm64);
                enc.emitMovRegReg(static_cast<uint8_t>(r), kRegRax);
            }
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
            if (r >= 0) {
                if (r != 0)
                    enc.movRaxFromReg(static_cast<uint8_t>(r));
                enc.testRaxRax();
            }
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
            auto it = pcToOffset.find(static_cast<size_t>(m.imm32));
            if (it == pcToOffset.end())
                break;
            int32_t rel = static_cast<int32_t>(it->second) - static_cast<int32_t>(enc.here() + 6);
            enc.jzRel32(rel);
            break;
        }
        case MirOp::JmpRel32: {
            auto it = pcToOffset.find(static_cast<size_t>(m.imm32));
            if (it == pcToOffset.end())
                break;
            int32_t rel = static_cast<int32_t>(it->second) - static_cast<int32_t>(enc.here() + 5);
            enc.jmpRel32(rel);
            break;
        }
        case MirOp::JleRel32: {
            auto it = pcToOffset.find(static_cast<size_t>(m.imm32));
            if (it == pcToOffset.end())
                break;
            int32_t rel = static_cast<int32_t>(it->second) - static_cast<int32_t>(enc.here() + 6);
            enc.jleRel32(rel);
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
        case MirOp::CallRax:
            enc.callRax();
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
}

} // namespace camel::jit::x64
