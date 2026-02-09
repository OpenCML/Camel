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
        case MirOp::CmpRaxFrameSetle:
            enc.cmpRaxFrameSetle(m.disp);
            break;
        case MirOp::CmpRaxMemAtSetle:
            enc.cmpRaxMemAtSetle(m.imm64);
            break;
        case MirOp::CmpRaxFrameSetl:
            enc.cmpRaxFrameSetl(m.disp);
            break;
        case MirOp::CmpRaxMemAtSetl:
            enc.cmpRaxMemAtSetl(m.imm64);
            break;
        case MirOp::CmpRaxFrameSetg:
            enc.cmpRaxFrameSetg(m.disp);
            break;
        case MirOp::CmpRaxMemAtSetg:
            enc.cmpRaxMemAtSetg(m.imm64);
            break;
        case MirOp::CmpRaxFrameSete:
            enc.cmpRaxFrameSete(m.disp);
            break;
        case MirOp::CmpRaxMemAtSete:
            enc.cmpRaxMemAtSete(m.imm64);
            break;
        case MirOp::CmpRaxFrameSetne:
            enc.cmpRaxFrameSetne(m.disp);
            break;
        case MirOp::CmpRaxMemAtSetne:
            enc.cmpRaxMemAtSetne(m.imm64);
            break;
        case MirOp::CmpRaxFrameSetge:
            enc.cmpRaxFrameSetge(m.disp);
            break;
        case MirOp::CmpRaxMemAtSetge:
            enc.cmpRaxMemAtSetge(m.imm64);
            break;
        case MirOp::MulRaxFromReg:
            enc.mulRaxFromReg(m.r0);
            break;
        case MirOp::MulRaxFromFrame:
            enc.mulRaxFromFrame(m.disp);
            break;
        case MirOp::MulRaxFromMemAt:
            enc.mulRaxFromMemAt(m.imm64);
            break;
        case MirOp::IdivRaxByReg:
            enc.idivRaxByReg(m.r0);
            break;
        case MirOp::IdivRaxByFrame:
            enc.idivRaxByFrame(m.disp);
            break;
        case MirOp::IdivRaxByMemAt:
            enc.idivRaxByMemAt(m.imm64);
            break;
        case MirOp::MulXmm0FromReg:
            enc.mulXmm0FromReg(m.r0);
            break;
        case MirOp::MulXmm0FromFrame:
            enc.mulXmm0FromFrame(m.disp);
            break;
        case MirOp::MulXmm0FromMemAt:
            enc.mulXmm0FromMemAt(m.imm64);
            break;
        case MirOp::DivXmm0FromReg:
            enc.divXmm0FromReg(m.r0);
            break;
        case MirOp::DivXmm0FromFrame:
            enc.divXmm0FromFrame(m.disp);
            break;
        case MirOp::DivXmm0FromMemAt:
            enc.divXmm0FromMemAt(m.imm64);
            break;
        case MirOp::ComisdXmm0FrameSetb:
            enc.comisdXmm0WithFrame(m.disp);
            enc.setbAlMovzxRax();
            break;
        case MirOp::ComisdXmm0MemAtSetb:
            enc.comisdXmm0WithMemAt(m.imm64);
            enc.setbAlMovzxRax();
            break;
        case MirOp::ComisdXmm0RegSetb:
            enc.comisdXmm0WithReg(m.r0);
            enc.setbAlMovzxRax();
            break;
        case MirOp::ComisdXmm0FrameSetbe:
            enc.comisdXmm0WithFrame(m.disp);
            enc.setbeAlMovzxRax();
            break;
        case MirOp::ComisdXmm0MemAtSetbe:
            enc.comisdXmm0WithMemAt(m.imm64);
            enc.setbeAlMovzxRax();
            break;
        case MirOp::ComisdXmm0RegSetbe:
            enc.comisdXmm0WithReg(m.r0);
            enc.setbeAlMovzxRax();
            break;
        case MirOp::ComisdXmm0FrameSete:
            enc.comisdXmm0WithFrame(m.disp);
            enc.setzAlMovzxRax();
            break;
        case MirOp::ComisdXmm0MemAtSete:
            enc.comisdXmm0WithMemAt(m.imm64);
            enc.setzAlMovzxRax();
            break;
        case MirOp::ComisdXmm0RegSete:
            enc.comisdXmm0WithReg(m.r0);
            enc.setzAlMovzxRax();
            break;
        case MirOp::ComisdXmm0FrameSeta:
            enc.comisdXmm0WithFrame(m.disp);
            enc.setaAlMovzxRax();
            break;
        case MirOp::ComisdXmm0MemAtSeta:
            enc.comisdXmm0WithMemAt(m.imm64);
            enc.setaAlMovzxRax();
            break;
        case MirOp::ComisdXmm0RegSeta:
            enc.comisdXmm0WithReg(m.r0);
            enc.setaAlMovzxRax();
            break;
        case MirOp::ComisdXmm0FrameSetae:
            enc.comisdXmm0WithFrame(m.disp);
            enc.setaeAlMovzxRax();
            break;
        case MirOp::ComisdXmm0MemAtSetae:
            enc.comisdXmm0WithMemAt(m.imm64);
            enc.setaeAlMovzxRax();
            break;
        case MirOp::ComisdXmm0RegSetae:
            enc.comisdXmm0WithReg(m.r0);
            enc.setaeAlMovzxRax();
            break;
        case MirOp::ComisdXmm0FrameSetnz:
            enc.comisdXmm0WithFrame(m.disp);
            enc.setnzAlMovzxRax();
            break;
        case MirOp::ComisdXmm0MemAtSetnz:
            enc.comisdXmm0WithMemAt(m.imm64);
            enc.setnzAlMovzxRax();
            break;
        case MirOp::ComisdXmm0RegSetnz:
            enc.comisdXmm0WithReg(m.r0);
            enc.setnzAlMovzxRax();
            break;
        case MirOp::MovEaxFromFrame:
            enc.movEaxFromFrame(m.disp);
            break;
        case MirOp::MovEaxFromReg:
            enc.movEaxFromReg(m.r0);
            break;
        case MirOp::MovEaxFromMemAt:
            enc.movEaxFromMemAt(m.imm64);
            break;
        case MirOp::AddEaxFromFrame:
            enc.addEaxFromFrame(m.disp);
            break;
        case MirOp::AddEaxFromReg:
            enc.addEaxFromReg(m.r0);
            break;
        case MirOp::AddEaxFromMemAt:
            enc.addEaxFromMemAt(m.imm64);
            break;
        case MirOp::SubEaxFromFrame:
            enc.subEaxFromFrame(m.disp);
            break;
        case MirOp::SubEaxFromReg:
            enc.subEaxFromReg(m.r0);
            break;
        case MirOp::SubEaxFromMemAt:
            enc.subEaxFromMemAt(m.imm64);
            break;
        case MirOp::MulEaxFromFrame:
            enc.mulEaxFromFrame(m.disp);
            break;
        case MirOp::MulEaxFromReg:
            enc.mulEaxFromReg(m.r0);
            break;
        case MirOp::MulEaxFromMemAt:
            enc.mulEaxFromMemAt(m.imm64);
            break;
        case MirOp::IdivEaxByFrame:
            enc.idivEaxByFrame(m.disp);
            break;
        case MirOp::IdivEaxByReg:
            enc.idivEaxByReg(m.r0);
            break;
        case MirOp::IdivEaxByMemAt:
            enc.idivEaxByMemAt(m.imm64);
            break;
        case MirOp::CmpEaxFrameSetl:
            enc.cmpEaxFrameSetl(m.disp);
            break;
        case MirOp::CmpEaxMemAtSetl:
            enc.cmpEaxMemAtSetl(m.imm64);
            break;
        case MirOp::CmpEaxRegSetl:
            enc.cmpEaxRegSetl(m.r0);
            break;
        case MirOp::CmpEaxFrameSetg:
            enc.cmpEaxFrameSetg(m.disp);
            break;
        case MirOp::CmpEaxMemAtSetg:
            enc.cmpEaxMemAtSetg(m.imm64);
            break;
        case MirOp::CmpEaxRegSetg:
            enc.cmpEaxRegSetg(m.r0);
            break;
        case MirOp::CmpEaxFrameSete:
            enc.cmpEaxFrameSete(m.disp);
            break;
        case MirOp::CmpEaxMemAtSete:
            enc.cmpEaxMemAtSete(m.imm64);
            break;
        case MirOp::CmpEaxRegSete:
            enc.cmpEaxRegSete(m.r0);
            break;
        case MirOp::CmpEaxFrameSetne:
            enc.cmpEaxFrameSetne(m.disp);
            break;
        case MirOp::CmpEaxMemAtSetne:
            enc.cmpEaxMemAtSetne(m.imm64);
            break;
        case MirOp::CmpEaxRegSetne:
            enc.cmpEaxRegSetne(m.r0);
            break;
        case MirOp::CmpEaxFrameSetle:
            enc.cmpEaxFrameSetle(m.disp);
            break;
        case MirOp::CmpEaxMemAtSetle:
            enc.cmpEaxMemAtSetle(m.imm64);
            break;
        case MirOp::CmpEaxRegSetle:
            enc.cmpEaxRegSetle(m.r0);
            break;
        case MirOp::CmpEaxFrameSetge:
            enc.cmpEaxFrameSetge(m.disp);
            break;
        case MirOp::CmpEaxMemAtSetge:
            enc.cmpEaxMemAtSetge(m.imm64);
            break;
        case MirOp::CmpEaxRegSetge:
            enc.cmpEaxRegSetge(m.r0);
            break;
        case MirOp::MovSsXmm0FromFrame:
            enc.movSsXmm0FromFrame(m.disp);
            break;
        case MirOp::MovSsXmm0FromReg:
            enc.movSsXmm0FromReg(m.r0);
            break;
        case MirOp::MovSsXmm0FromMemAt:
            enc.movSsXmm0FromMemAt(m.imm64);
            break;
        case MirOp::AddSsXmm0FromFrame:
            enc.addSsXmm0FromFrame(m.disp);
            break;
        case MirOp::AddSsXmm0FromReg:
            enc.addSsXmm0FromReg(m.r0);
            break;
        case MirOp::AddSsXmm0FromMemAt:
            enc.addSsXmm0FromMemAt(m.imm64);
            break;
        case MirOp::SubSsXmm0FromFrame:
            enc.subSsXmm0FromFrame(m.disp);
            break;
        case MirOp::SubSsXmm0FromReg:
            enc.subSsXmm0FromReg(m.r0);
            break;
        case MirOp::SubSsXmm0FromMemAt:
            enc.subSsXmm0FromMemAt(m.imm64);
            break;
        case MirOp::MulSsXmm0FromFrame:
            enc.mulSsXmm0FromFrame(m.disp);
            break;
        case MirOp::MulSsXmm0FromReg:
            enc.mulSsXmm0FromReg(m.r0);
            break;
        case MirOp::MulSsXmm0FromMemAt:
            enc.mulSsXmm0FromMemAt(m.imm64);
            break;
        case MirOp::DivSsXmm0FromFrame:
            enc.divSsXmm0FromFrame(m.disp);
            break;
        case MirOp::DivSsXmm0FromReg:
            enc.divSsXmm0FromReg(m.r0);
            break;
        case MirOp::DivSsXmm0FromMemAt:
            enc.divSsXmm0FromMemAt(m.imm64);
            break;
        case MirOp::MovSsFrameFromXmm0:
            enc.movSsFrameFromXmm0(m.disp);
            break;
        case MirOp::MovSsRegFromXmm0:
            enc.movSsRegFromXmm0(m.r0);
            break;
        case MirOp::ComissXmm0FrameSetb:
            enc.comissXmm0WithFrame(m.disp);
            enc.setbAlMovzxRax();
            break;
        case MirOp::ComissXmm0MemAtSetb:
            enc.comissXmm0WithMemAt(m.imm64);
            enc.setbAlMovzxRax();
            break;
        case MirOp::ComissXmm0RegSetb:
            enc.comissXmm0WithReg(m.r0);
            enc.setbAlMovzxRax();
            break;
        case MirOp::ComissXmm0FrameSetbe:
            enc.comissXmm0WithFrame(m.disp);
            enc.setbeAlMovzxRax();
            break;
        case MirOp::ComissXmm0MemAtSetbe:
            enc.comissXmm0WithMemAt(m.imm64);
            enc.setbeAlMovzxRax();
            break;
        case MirOp::ComissXmm0RegSetbe:
            enc.comissXmm0WithReg(m.r0);
            enc.setbeAlMovzxRax();
            break;
        case MirOp::ComissXmm0FrameSete:
            enc.comissXmm0WithFrame(m.disp);
            enc.setzAlMovzxRax();
            break;
        case MirOp::ComissXmm0MemAtSete:
            enc.comissXmm0WithMemAt(m.imm64);
            enc.setzAlMovzxRax();
            break;
        case MirOp::ComissXmm0RegSete:
            enc.comissXmm0WithReg(m.r0);
            enc.setzAlMovzxRax();
            break;
        case MirOp::ComissXmm0FrameSeta:
            enc.comissXmm0WithFrame(m.disp);
            enc.setaAlMovzxRax();
            break;
        case MirOp::ComissXmm0MemAtSeta:
            enc.comissXmm0WithMemAt(m.imm64);
            enc.setaAlMovzxRax();
            break;
        case MirOp::ComissXmm0RegSeta:
            enc.comissXmm0WithReg(m.r0);
            enc.setaAlMovzxRax();
            break;
        case MirOp::ComissXmm0FrameSetae:
            enc.comissXmm0WithFrame(m.disp);
            enc.setaeAlMovzxRax();
            break;
        case MirOp::ComissXmm0MemAtSetae:
            enc.comissXmm0WithMemAt(m.imm64);
            enc.setaeAlMovzxRax();
            break;
        case MirOp::ComissXmm0RegSetae:
            enc.comissXmm0WithReg(m.r0);
            enc.setaeAlMovzxRax();
            break;
        case MirOp::ComissXmm0FrameSetnz:
            enc.comissXmm0WithFrame(m.disp);
            enc.setnzAlMovzxRax();
            break;
        case MirOp::ComissXmm0MemAtSetnz:
            enc.comissXmm0WithMemAt(m.imm64);
            enc.setnzAlMovzxRax();
            break;
        case MirOp::ComissXmm0RegSetnz:
            enc.comissXmm0WithReg(m.r0);
            enc.setnzAlMovzxRax();
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
        case MirOp::JzRel32: {
            auto it = pcToOffset.find(static_cast<size_t>(m.imm32));
            if (it == pcToOffset.end())
                break;
            int32_t rel = static_cast<int32_t>(it->second) - static_cast<int32_t>(enc.here() + 6);
            enc.jzRel32(rel);
            break;
        }
        case MirOp::CmoveRcxFromRbx:
            enc.cmoveRcxFromRbx();
            break;
        case MirOp::CmoveR8FromR9:
            enc.cmoveR8FromR9();
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
