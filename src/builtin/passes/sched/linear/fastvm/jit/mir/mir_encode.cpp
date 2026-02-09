/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 */

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_encode.h"

namespace camel::jit::x64 {

void encodeMirBuffer(
    const MirBuffer &buf, const std::unordered_map<size_t, size_t> &pcToOffset,
    std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset) {
    Encoder enc(code, asmOut, baseOffset);
    for (const Mir &m : buf) {
        switch (m.op) {
        case MirOp::MovRegReg:
            enc.emitMovRegReg(m.r0, m.r1);
            break;
        case MirOp::MovRegImm32:
            enc.emitMovRegImm32(m.r0, m.imm32);
            break;
        case MirOp::MovRegImm64:
            enc.emitMovRegImm64(m.r0, m.imm64);
            break;
        case MirOp::MovRegFromFrame:
            enc.movRegFromFrame(m.r0, m.disp);
            break;
        case MirOp::MovFrameFromReg:
            enc.movToFrame(m.disp, m.r0);
            break;
        case MirOp::MovRaxFromMemAt:
            enc.movRaxFromMemAt(m.imm64);
            break;
        case MirOp::AddRaxFromMemAt:
            enc.addRaxFromMemAt(m.imm64);
            break;
        case MirOp::SubRaxFromMemAt:
            enc.subRaxFromMemAt(m.imm64);
            break;
        case MirOp::MovRaxFromReg:
            enc.movRaxFromReg(m.r0);
            break;
        case MirOp::MovRegFromRax:
            enc.movRegFromRax(m.r0);
            break;
        case MirOp::AddRaxFromReg:
            enc.addRaxFromReg(m.r0);
            break;
        case MirOp::SubRaxFromReg:
            enc.subRaxFromReg(m.r0);
            break;
        case MirOp::AddRaxFromFrame:
            enc.addRaxFromFrame(m.disp);
            break;
        case MirOp::SubRaxFromFrame:
            enc.subRaxFromFrame(m.disp);
            break;
        case MirOp::MovXmm0FromFrame:
            enc.movXmm0FromFrame(m.disp);
            break;
        case MirOp::AddXmm0FromFrame:
            enc.addXmm0FromFrame(m.disp);
            break;
        case MirOp::SubXmm0FromFrame:
            enc.subXmm0FromFrame(m.disp);
            break;
        case MirOp::MovFrameFromXmm0:
            enc.movFrameFromXmm0(m.disp);
            break;
        case MirOp::MovXmm0FromReg:
            enc.movXmm0FromReg(m.r0);
            break;
        case MirOp::AddXmm0FromReg:
            enc.addXmm0FromReg(m.r0);
            break;
        case MirOp::SubXmm0FromReg:
            enc.subXmm0FromReg(m.r0);
            break;
        case MirOp::MovRegFromXmm0:
            enc.movRegFromXmm0(m.r0);
            break;
        case MirOp::MovXmm0FromMemAt:
            enc.movXmm0FromMemAt(m.imm64);
            break;
        case MirOp::AddXmm0FromMemAt:
            enc.addXmm0FromMemAt(m.imm64);
            break;
        case MirOp::SubXmm0FromMemAt:
            enc.subXmm0FromMemAt(m.imm64);
            break;
        case MirOp::CmpRaxImm8Setle:
            enc.cmpRaxImm8Setle();
            break;
        case MirOp::TestRaxRax:
            enc.testRaxRax();
            break;
        case MirOp::TestRaxJzRel32: {
            auto it = pcToOffset.find(static_cast<size_t>(m.imm32));
            if (it == pcToOffset.end())
                break;
            int32_t rel =
                static_cast<int32_t>(it->second) - static_cast<int32_t>(enc.here() + 3 + 6);
            enc.testRaxJzRel32(rel);
            break;
        }
        case MirOp::CmoveRcxFromRbx:
            enc.cmoveRcxFromRbx();
            break;
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
        }
    }
}

} // namespace camel::jit::x64
