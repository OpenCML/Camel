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

#include "mir_encode.h"
#include "../regalloc/regalloc.h"

#include <vector>

namespace camel::jit::x64 {

using ::camel::jit::kSpilled;
using ::camel::jit::VRegAllocation;

struct JumpPatch {
    size_t jumpPos;        // 跳转指令起始偏移
    size_t targetMirIndex; // 目标 MIR 下标（唯一对应 startOffset[targetMirIndex]）
    int instrLen;          // 跳转指令长度（用于定位 rel32 写入位置：jmp=1, jz/jle=2）
    size_t asmLineIndex;   // 对应 asm 行下标，修补后更新 rel 显示
    size_t jumpEndPos;     // 编码后“下一条指令”偏移，rel = targetOffset - jumpEndPos
};

void encodeMirBuffer(
    const MirBuffer &buf, std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset,
    const VRegAllocation *vregAlloc,
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries,
    void *debugTraceFn) {
    struct SpillState {
        // The encoder does not materialize full spill code for every MIR op.
        // Instead it remembers a few common linear patterns so later ops can
        // recover frame-backed values without reloading blindly.
        int loadVReg        = -1;
        int loadDisp        = 0;
        int copyVReg        = -1;
        int copyDisp        = 0;
        int destVReg        = -1;
        int destDefaultVReg = -1;

        void clearTransientForVReg(int vreg) {
            if (loadVReg == vreg)
                loadVReg = -1;
            if (copyVReg == vreg)
                copyVReg = -1;
        }

        void rememberLoad(int vreg, int disp) {
            if (loadVReg >= 0 && loadVReg != vreg && copyVReg < 0) {
                copyVReg = loadVReg;
                copyDisp = loadDisp;
            }
            loadVReg = vreg;
            loadDisp = disp;
        }

        bool hasCopiedValue(int vreg) const { return copyVReg == vreg; }

        void clearCopiedValue() { copyVReg = -1; }

        void clearDeferredLoad() { loadVReg = -1; }

        void clearSpilledDest() { destVReg = -1; }

        void rememberSpilledDest(int vreg, int defaultVreg) {
            destVReg        = vreg;
            destDefaultVReg = defaultVreg;
        }

        bool matchesSpilledDest(int vreg) const { return destVReg == vreg; }
    };

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
    SpillState spillState;
    for (size_t i = 0; i < buf.size(); ++i) {
        const Mir &m   = buf[i];
        startOffset[i] = enc.here();
        switch (m.op) {
        case MirOp::VLoadFromFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movRegFromFrame(static_cast<uint8_t>(r), m.disp);
                spillState.clearTransientForVReg(static_cast<int>(m.r0));
            } else {
                spillState.rememberLoad(static_cast<int>(m.r0), m.disp);
            }
            break;
        }
        case MirOp::VStoreToFrame: {
            int r = pregFor(static_cast<VRegId>(m.r0));
            if (r >= 0) {
                enc.movToFrame(m.disp, static_cast<uint8_t>(r));
                spillState.clearSpilledDest();
            } else if (spillState.matchesSpilledDest(static_cast<int>(m.r0))) {
                // BRCH 的 vRes 被 spill：VCmovnz 已把 0/1 物化到 rbx，这里写回 frame 并恢复 rbx
                enc.movToFrame(m.disp, kRegRbx);
                enc.popRbx();
                spillState.clearSpilledDest();
            }
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
            if (dr >= 0 && sr >= 0 && dr == sr)
                break;
            if (dr >= 0 && sr >= 0) {
                enc.emitMovRegReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(sr));
                spillState.clearSpilledDest();
            } else if (dr >= 0 && sr < 0) {
                // 源被 spill：从 frame 加载到目标 reg（JOIN 的 w0 物化到 v3），避免 v3 沿用残留值
                if (spillState.hasCopiedValue(static_cast<int>(m.r1))) {
                    enc.movRegFromFrame(static_cast<uint8_t>(dr), spillState.copyDisp);
                    spillState.clearCopiedValue();
                }
                spillState.clearSpilledDest();
            } else if (dr < 0) {
                spillState.rememberSpilledDest(static_cast<int>(m.r0), static_cast<int>(m.r1));
            }
            break;
        }
        case MirOp::VTest: {
            // VTest is used both for ordinary boolean values and JOIN's
            // synthetic branch index. Passing an optional frame displacement lets
            // us recover the tested value even when the source vreg never got a
            // physical register.
            // JOIN 传入 m.disp（dIdx）时从 frame 加载并 test，不依赖 v2 的 reg，避免 VCopy(v3,v0)
            // 与 v2 同 reg 时覆盖 idx
            int disp = -1;
            if (m.disp != 0) {
                disp = m.disp;
            } else {
                int r = pregFor(static_cast<VRegId>(m.r0));
                if (r >= 0) {
                    enc.testRegReg(static_cast<uint8_t>(r));
                    spillState.clearDeferredLoad();
                    break;
                }
                if (spillState.loadVReg == static_cast<int>(m.r0))
                    disp = spillState.loadDisp;
            }
            if (disp >= 0) {
                enc.pushRbx();
                enc.movRegFromFrame(kRegRbx, disp);
                enc.testRegReg(kRegRbx);
                enc.popRbx();
            }
            spillState.clearDeferredLoad();
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
            if (dr >= 0 && sr >= 0) {
                enc.cmovnzRegFromReg(static_cast<uint8_t>(dr), static_cast<uint8_t>(sr));
                spillState.clearSpilledDest();
            } else if (dr < 0 && spillState.matchesSpilledDest(static_cast<int>(m.r0))) {
                int defReg = pregFor(static_cast<VRegId>(spillState.destDefaultVReg));
                if (defReg >= 0 && sr >= 0) {
                    enc.pushRbx();
                    enc.emitMovRegReg(kRegRbx, static_cast<uint8_t>(defReg));
                    enc.cmovnzRegFromReg(kRegRbx, static_cast<uint8_t>(sr));
                } else {
                    spillState.clearSpilledDest(); // 无法物化，避免 VStoreToFrame 误用 rbx
                }
            }
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
                enc.cmpRegReg(static_cast<uint8_t>(lr), static_cast<uint8_t>(rr));
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
                switch (m.op) {
                case MirOp::VCmpSetLImm:
                    enc.setlAlMovzxRax();
                    break;
                case MirOp::VCmpSetLEImm:
                    enc.setleAlMovzxRax();
                    break;
                case MirOp::VCmpSetGImm:
                    enc.setgAlMovzxRax();
                    break;
                case MirOp::VCmpSetGEImm:
                    enc.setgeAlMovzxRax();
                    break;
                case MirOp::VCmpSetEImm:
                    enc.setzAlMovzxRax();
                    break;
                case MirOp::VCmpSetNEImm:
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
        case MirOp::NativeJitFuncCall: {
            auto *p = reinterpret_cast<const NativeJitCallParams *>(m.imm64);

            if (p->frameless) {
                // ═══ FRAMELESS PATH: stack-based self-call (no Frame pool) ═══
                // Phase L: args may already be in physical registers via pre-loaded VRegs.
                static constexpr uint8_t argTmpRegs[] =
                    {kRegRax, kRegRcx, kRegRdx, kRegR8, kRegR9, kRegR10, kRegR11};
                uint8_t nArgs = p->argsCnt < 7 ? p->argsCnt : 7;
                uint8_t actualArgRegs[7];
                for (uint8_t ai = 0; ai < nArgs; ++ai) {
                    int preloadedReg = -1;
                    if (p->argVRegs[ai] != 0xFF)
                        preloadedReg = pregFor(static_cast<VRegId>(p->argVRegs[ai]));
                    if (preloadedReg >= 0) {
                        actualArgRegs[ai] = static_cast<uint8_t>(preloadedReg);
                    } else {
                        enc.movRegFromFrame(argTmpRegs[ai], p->argSrcDisps[ai]);
                        actualArgRegs[ai] = argTmpRegs[ai];
                    }
                }

                enc.pushRdi();
                enc.subRspImm(p->calleeSlotBytes);
                enc.movRdiRsp();

                // The callee frame uses the same slot layout as the current
                // graph, so arguments can be copied into slots 1..N directly.
                for (uint8_t ai = 0; ai < nArgs; ++ai)
                    enc.movToFrame(static_cast<int>((ai + 1) * sizeof(slot_t)), actualArgRegs[ai]);

                patches.push_back({enc.here(), 0, 5, 0, 0});
                enc.callRel32(0);
                patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
                patches.back().jumpEndPos   = enc.here();

                enc.addRspImm(p->calleeSlotBytes);
                enc.popRdi();
                if (p->resultVReg != 0xFF) {
                    // Result-visible mode: keep the call result in a vreg so a
                    // following VStore/VLoad pair can be optimized away upstream.
                    int preg = pregFor(static_cast<VRegId>(p->resultVReg));
                    if (preg >= 0 && preg != static_cast<int>(kRegRax))
                        enc.emitMovRegReg(static_cast<uint8_t>(preg), kRegRax);
                } else {
                    enc.movToFrame(p->resultDisp, kRegRax);
                }
                break;
            }

            // ═══ FRAME-BASED PATH (cross-graph or non-frameless) ═══
            enc.pushRdi();

            size_t jnzNotCompiledRelPos = 0, jnzNotCompiledEnd = 0;
            if (!p->isSameGraph) {
                enc.movRaxImm64(p->fastop1Addr);
                enc.emitBytes({0x0F, 0xB6, 0x00});
                enc.asmLine("movzx eax, byte [rax]  ; fastop[1]");
                enc.testRaxRax();
                jnzNotCompiledRelPos = enc.jneRel32(0);
                jnzNotCompiledEnd    = enc.here();
                enc.movRaxImm64(p->extra2Addr);
                enc.emitBytes({0x48, 0x8B, 0x00});
                enc.asmLine("mov rax, [rax]  ; load extra2");
                enc.shlRax16();
                enc.shrRax16();
                enc.pushRax();
            }

            // ═══ Frame acquire check ═══
            enc.movR10FromRbx();
            enc.movR11FromR10Disp8(8);
            if (p->isSameGraph) {
                enc.cmpR11R12();
            } else {
                enc.movRaxImm64(p->targetGraphAddr);
                enc.cmpR11Rax();
            }
            size_t jneRelPos = enc.jneRel32(0);
            size_t jneEnd    = enc.here();

            // ═══ FAST PATH: acquire frame from pool and enter compiled callee ═══
            enc.movR11FromR10Disp8(16);
            enc.movRbxFromR11();
            enc.leaR11R10Disp8(40);
            enc.movR11FromR10Store();
            for (uint8_t ai = 0; ai < p->argsCnt; ++ai) {
                enc.movRaxFromFrame(p->argSrcDisps[ai]);
                enc.movR11Disp8FromRax(static_cast<int8_t>((ai + 1) * 8));
            }
            enc.movRdiR11();

            if (p->isSameGraph) {
                patches.push_back({enc.here(), 0, 5, 0, 0});
                enc.callRel32(0);
                patches.back().asmLineIndex = enc.getAsmLineCount() - 1;
                patches.back().jumpEndPos   = enc.here();
            } else {
                enc.popRax();
                enc.movRcxRdi();
                enc.movRdxRsi();
                enc.subRspShadow();
                enc.emitBytes({0xff, 0xd0});
                enc.asmLine("call rax  ; cross-graph C++ entry");
                enc.addRspShadow();
            }

            // ═══ Release frame + store result ═══
            enc.movR10FromRdi();
            enc.popRdi();
            enc.movR11FromRbx();
            enc.movR10Disp8FromR11(16);
            enc.movRbxFromR10();
            enc.movToFrame(p->resultDisp, 0);
            size_t jmpFastPos = enc.here();
            enc.jmpRel32(0);
            size_t jmpFastEnd = enc.here();

            // ═══ SLOW PATH ═══
            // Reuse the interpreter/trampoline helper when the frame pool cannot
            // provide a compatible frame or the target graph is not compiled yet.
            size_t slowStart = enc.here();
            if (!p->isSameGraph) {
                enc.popRax();
                size_t slowCommon = enc.here();
                enc.patchRel32At(
                    jnzNotCompiledRelPos,
                    static_cast<int32_t>(slowCommon - jnzNotCompiledEnd));
                enc.patchRel32At(jneRelPos, static_cast<int32_t>(slowStart - jneEnd));
            } else {
                enc.patchRel32At(jneRelPos, static_cast<int32_t>(slowStart - jneEnd));
            }

            if (p->isSameGraph) {
                enc.movRcxRdi();
                enc.movRdxRsi();
                enc.movR8Imm64(p->slowPathBcAddr);
            } else {
                enc.movRcxRdi();
                enc.movRdxRsi();
                enc.emitMovRegImm32(4, p->slowPathPc);
            }
            enc.movRaxImm64(p->slowPathFnAddr);
            enc.callRax();
            enc.popRdi();
            enc.movToFrame(p->resultDisp, 0);

            // ═══ DONE ═══
            size_t donePos = enc.here();
            enc.patchRel32At(jmpFastPos + 1, static_cast<int32_t>(donePos - jmpFastEnd));
            break;
        }
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
