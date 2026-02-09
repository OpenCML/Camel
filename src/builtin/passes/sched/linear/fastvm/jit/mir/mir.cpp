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

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"

#include <iomanip>
#include <sstream>

namespace camel::jit::x64 {

static bool fitsDisp8(int32_t d) { return d >= -128 && d <= 127; }

std::string mirToString(const Mir &m) {
    std::ostringstream os;
    switch (m.op) {
    case MirOp::MovRegReg:
        os << "mov " << mirRegName(m.r0) << ", " << mirRegName(m.r1);
        break;
    case MirOp::MovRegImm32:
        os << "mov " << mirRegName(m.r0) << "d, " << m.imm32;
        break;
    case MirOp::MovRegImm64: {
        os << "mov " << mirRegName(m.r0) << ", 0x" << std::hex << m.imm64 << std::dec;
        break;
    }
    case MirOp::MovRegFromFrame:
        os << "mov " << mirRegName(m.r0) << ", [rdi+" << m.disp << "]";
        break;
    case MirOp::MovFrameFromReg:
        os << "mov [rdi+" << m.disp << "], " << mirRegName(m.r0);
        break;
    case MirOp::MovRaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; mov rax, [rbx]";
        break;
    case MirOp::AddRaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; add rax, [rbx]";
        break;
    case MirOp::SubRaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; sub rax, [rbx]";
        break;
    case MirOp::MovRaxFromReg:
        os << "mov rax, " << mirRegName(m.r0);
        break;
    case MirOp::MovRegFromRax:
        os << "mov " << mirRegName(m.r0) << ", rax";
        break;
    case MirOp::AddRaxFromReg:
        os << "add rax, " << mirRegName(m.r0);
        break;
    case MirOp::SubRaxFromReg:
        os << "sub rax, " << mirRegName(m.r0);
        break;
    case MirOp::AddRaxFromFrame:
        os << "add rax, [rdi+" << m.disp << "]";
        break;
    case MirOp::SubRaxFromFrame:
        os << "sub rax, [rdi+" << m.disp << "]";
        break;
    case MirOp::MovXmm0FromFrame:
        os << "movsd xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::AddXmm0FromFrame:
        os << "addsd xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::SubXmm0FromFrame:
        os << "subsd xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::MovFrameFromXmm0:
        os << "movsd [rdi+" << m.disp << "], xmm0";
        break;
    case MirOp::MovXmm0FromReg:
        os << "movq xmm0, " << mirRegName(m.r0);
        break;
    case MirOp::AddXmm0FromReg:
        os << "movq xmm1, " << mirRegName(m.r0) << "; addsd xmm0, xmm1";
        break;
    case MirOp::SubXmm0FromReg:
        os << "movq xmm1, " << mirRegName(m.r0) << "; subsd xmm0, xmm1";
        break;
    case MirOp::MovRegFromXmm0:
        os << "movq " << mirRegName(m.r0) << ", xmm0";
        break;
    case MirOp::MovXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; movsd xmm0, [rbx]";
        break;
    case MirOp::AddXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; addsd xmm0, [rbx]";
        break;
    case MirOp::SubXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; subsd xmm0, [rbx]";
        break;
    case MirOp::CmpRaxImm8Setle:
        os << "cmp rax, 1; setle al; movzx rax, al";
        break;
    case MirOp::CmpRaxFrameSetle:
        os << "cmp rax, [rdi+" << m.disp << "]; setle al; movzx rax, al";
        break;
    case MirOp::CmpRaxMemAtSetle:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp rax, [rbx]; setle al; movzx rax, al";
        break;
    case MirOp::CmpRaxFrameSetl:
        os << "cmp rax, [rdi+" << m.disp << "]; setl al; movzx rax, al";
        break;
    case MirOp::CmpRaxMemAtSetl:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp rax, [rbx]; setl al; movzx rax, al";
        break;
    case MirOp::CmpRaxFrameSetg:
        os << "cmp rax, [rdi+" << m.disp << "]; setg al; movzx rax, al";
        break;
    case MirOp::CmpRaxMemAtSetg:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp rax, [rbx]; setg al; movzx rax, al";
        break;
    case MirOp::CmpRaxFrameSete:
        os << "cmp rax, [rdi+" << m.disp << "]; sete al; movzx rax, al";
        break;
    case MirOp::CmpRaxMemAtSete:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp rax, [rbx]; sete al; movzx rax, al";
        break;
    case MirOp::CmpRaxFrameSetne:
        os << "cmp rax, [rdi+" << m.disp << "]; setne al; movzx rax, al";
        break;
    case MirOp::CmpRaxMemAtSetne:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp rax, [rbx]; setne al; movzx rax, al";
        break;
    case MirOp::CmpRaxFrameSetge:
        os << "cmp rax, [rdi+" << m.disp << "]; setge al; movzx rax, al";
        break;
    case MirOp::CmpRaxMemAtSetge:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp rax, [rbx]; setge al; movzx rax, al";
        break;
    case MirOp::MulRaxFromReg:
        os << "imul rax, " << mirRegName(m.r0);
        break;
    case MirOp::MulRaxFromFrame:
        os << "imul rax, [rdi+" << m.disp << "]";
        break;
    case MirOp::MulRaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; imul rax, [rbx]";
        break;
    case MirOp::IdivRaxByReg:
        os << "cqo; idiv " << mirRegName(m.r0);
        break;
    case MirOp::IdivRaxByFrame:
        os << "cqo; idiv qword [rdi+" << m.disp << "]";
        break;
    case MirOp::IdivRaxByMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; cqo; idiv [rbx]";
        break;
    case MirOp::MulXmm0FromReg:
        os << "movq xmm1, " << mirRegName(m.r0) << "; mulsd xmm0, xmm1";
        break;
    case MirOp::MulXmm0FromFrame:
        os << "mulsd xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::MulXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; mulsd xmm0, [rbx]";
        break;
    case MirOp::DivXmm0FromReg:
        os << "movq xmm1, " << mirRegName(m.r0) << "; divsd xmm0, xmm1";
        break;
    case MirOp::DivXmm0FromFrame:
        os << "divsd xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::DivXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; divsd xmm0, [rbx]";
        break;
    case MirOp::ComisdXmm0FrameSetb:
        os << "comisd xmm0, [rdi+" << m.disp << "]; setb al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0MemAtSetb:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comisd xmm0, [rbx]; setb al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0RegSetb:
        os << "movq xmm1, " << mirRegName(m.r0) << "; comisd xmm0, xmm1; setb al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0FrameSetbe:
        os << "comisd xmm0, [rdi+" << m.disp << "]; setbe al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0MemAtSetbe:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comisd xmm0, [rbx]; setbe al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0RegSetbe:
        os << "movq xmm1, " << mirRegName(m.r0) << "; comisd xmm0, xmm1; setbe al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0FrameSete:
        os << "comisd xmm0, [rdi+" << m.disp << "]; sete al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0MemAtSete:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comisd xmm0, [rbx]; sete al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0RegSete:
        os << "movq xmm1, " << mirRegName(m.r0) << "; comisd xmm0, xmm1; sete al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0FrameSeta:
        os << "comisd xmm0, [rdi+" << m.disp << "]; seta al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0MemAtSeta:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comisd xmm0, [rbx]; seta al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0RegSeta:
        os << "movq xmm1, " << mirRegName(m.r0) << "; comisd xmm0, xmm1; seta al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0FrameSetae:
        os << "comisd xmm0, [rdi+" << m.disp << "]; setae al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0MemAtSetae:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comisd xmm0, [rbx]; setae al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0RegSetae:
        os << "movq xmm1, " << mirRegName(m.r0) << "; comisd xmm0, xmm1; setae al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0FrameSetnz:
        os << "comisd xmm0, [rdi+" << m.disp << "]; setnz al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0MemAtSetnz:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comisd xmm0, [rbx]; setnz al; movzx rax, al";
        break;
    case MirOp::ComisdXmm0RegSetnz:
        os << "movq xmm1, " << mirRegName(m.r0) << "; comisd xmm0, xmm1; setnz al; movzx rax, al";
        break;
    case MirOp::MovEaxFromFrame:
        os << "mov eax, [rdi+" << m.disp << "]";
        break;
    case MirOp::MovEaxFromReg:
        os << "mov eax, " << mirRegName(m.r0);
        break;
    case MirOp::MovEaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; mov eax, [rbx]";
        break;
    case MirOp::AddEaxFromFrame:
        os << "add eax, [rdi+" << m.disp << "]";
        break;
    case MirOp::AddEaxFromReg:
        os << "add eax, " << mirRegName(m.r0);
        break;
    case MirOp::AddEaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; add eax, [rbx]";
        break;
    case MirOp::SubEaxFromFrame:
        os << "sub eax, [rdi+" << m.disp << "]";
        break;
    case MirOp::SubEaxFromReg:
        os << "sub eax, " << mirRegName(m.r0);
        break;
    case MirOp::SubEaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; sub eax, [rbx]";
        break;
    case MirOp::MulEaxFromFrame:
        os << "imul eax, [rdi+" << m.disp << "]";
        break;
    case MirOp::MulEaxFromReg:
        os << "imul eax, " << mirRegName(m.r0);
        break;
    case MirOp::MulEaxFromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; imul eax, [rbx]";
        break;
    case MirOp::IdivEaxByFrame:
        os << "cdq; idiv dword [rdi+" << m.disp << "]";
        break;
    case MirOp::IdivEaxByReg:
        os << "cdq; idiv " << mirRegName(m.r0);
        break;
    case MirOp::IdivEaxByMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; cdq; idiv [rbx]";
        break;
    case MirOp::CmpEaxFrameSetl:
    case MirOp::CmpEaxFrameSetg:
    case MirOp::CmpEaxFrameSete:
    case MirOp::CmpEaxFrameSetne:
    case MirOp::CmpEaxFrameSetle:
    case MirOp::CmpEaxFrameSetge:
        os << "cmp eax, [rdi+" << m.disp << "]; setcc al; movzx rax, al";
        break;
    case MirOp::CmpEaxMemAtSetl:
    case MirOp::CmpEaxMemAtSetg:
    case MirOp::CmpEaxMemAtSete:
    case MirOp::CmpEaxMemAtSetne:
    case MirOp::CmpEaxMemAtSetle:
    case MirOp::CmpEaxMemAtSetge:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; cmp eax, [rbx]; setcc al; movzx rax, al";
        break;
    case MirOp::CmpEaxRegSetl:
    case MirOp::CmpEaxRegSetg:
    case MirOp::CmpEaxRegSete:
    case MirOp::CmpEaxRegSetne:
    case MirOp::CmpEaxRegSetle:
    case MirOp::CmpEaxRegSetge:
        os << "cmp eax, " << mirRegName(m.r0) << "; setcc al; movzx rax, al";
        break;
    case MirOp::MovSsXmm0FromFrame:
        os << "movss xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::MovSsXmm0FromReg:
        os << "movd xmm0, " << mirRegName(m.r0);
        break;
    case MirOp::MovSsXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; movss xmm0, [rbx]";
        break;
    case MirOp::AddSsXmm0FromFrame:
        os << "addss xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::AddSsXmm0FromReg:
        os << "movd xmm1, " << mirRegName(m.r0) << "; addss xmm0, xmm1";
        break;
    case MirOp::AddSsXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; addss xmm0, [rbx]";
        break;
    case MirOp::SubSsXmm0FromFrame:
        os << "subss xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::SubSsXmm0FromReg:
        os << "movd xmm1, " << mirRegName(m.r0) << "; subss xmm0, xmm1";
        break;
    case MirOp::SubSsXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; subss xmm0, [rbx]";
        break;
    case MirOp::MulSsXmm0FromFrame:
        os << "mulss xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::MulSsXmm0FromReg:
        os << "movd xmm1, " << mirRegName(m.r0) << "; mulss xmm0, xmm1";
        break;
    case MirOp::MulSsXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; mulss xmm0, [rbx]";
        break;
    case MirOp::DivSsXmm0FromFrame:
        os << "divss xmm0, [rdi+" << m.disp << "]";
        break;
    case MirOp::DivSsXmm0FromReg:
        os << "movd xmm1, " << mirRegName(m.r0) << "; divss xmm0, xmm1";
        break;
    case MirOp::DivSsXmm0FromMemAt:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec << "; divss xmm0, [rbx]";
        break;
    case MirOp::MovSsFrameFromXmm0:
        os << "movss [rdi+" << m.disp << "], xmm0";
        break;
    case MirOp::MovSsRegFromXmm0:
        os << "movd " << mirRegName(m.r0) << ", xmm0";
        break;
    case MirOp::ComissXmm0FrameSetb:
    case MirOp::ComissXmm0FrameSetbe:
    case MirOp::ComissXmm0FrameSete:
    case MirOp::ComissXmm0FrameSeta:
    case MirOp::ComissXmm0FrameSetae:
    case MirOp::ComissXmm0FrameSetnz:
        os << "comiss xmm0, [rdi+" << m.disp << "]; setcc al; movzx rax, al";
        break;
    case MirOp::ComissXmm0MemAtSetb:
    case MirOp::ComissXmm0MemAtSetbe:
    case MirOp::ComissXmm0MemAtSete:
    case MirOp::ComissXmm0MemAtSeta:
    case MirOp::ComissXmm0MemAtSetae:
    case MirOp::ComissXmm0MemAtSetnz:
        os << "mov rbx, 0x" << std::hex << m.imm64 << std::dec
           << "; comiss xmm0, [rbx]; setcc al; movzx rax, al";
        break;
    case MirOp::ComissXmm0RegSetb:
    case MirOp::ComissXmm0RegSetbe:
    case MirOp::ComissXmm0RegSete:
    case MirOp::ComissXmm0RegSeta:
    case MirOp::ComissXmm0RegSetae:
    case MirOp::ComissXmm0RegSetnz:
        os << "comiss xmm0, " << mirRegName(m.r0) << "; setcc al; movzx rax, al";
        break;
    case MirOp::TestRaxRax:
        os << "test rax, rax";
        break;
    case MirOp::TestRaxJzRel32:
        os << "test rax, rax; jz pc=" << m.imm32;
        break;
    case MirOp::CmoveRcxFromRbx:
        os << "cmove rcx, rbx";
        break;
    case MirOp::JmpRel32:
        os << "jmp pc=" << m.imm32;
        break;
    case MirOp::JleRel32:
        os << "jle pc=" << m.imm32;
        break;
    case MirOp::JmpRel8:
        os << "jmp rel8 " << m.disp;
        break;
    case MirOp::JleRel8:
        os << "jle rel8 " << m.disp;
        break;
    case MirOp::Ret:
        os << "ret";
        break;
    case MirOp::CallRax:
        os << "call rax";
        break;
    case MirOp::Nop:
        os << "nop";
        break;
    }
    return os.str();
}

// 仅用于帧访问的 op 才带 [rdi+disp]，需槽位注释
static bool mirOpUsesFrameDisp(MirOp op) {
    switch (op) {
    case MirOp::CmpRaxFrameSetle:
    case MirOp::CmpRaxFrameSetl:
    case MirOp::CmpRaxFrameSetg:
    case MirOp::CmpRaxFrameSete:
    case MirOp::CmpRaxFrameSetne:
    case MirOp::CmpRaxFrameSetge:
    case MirOp::MulRaxFromFrame:
    case MirOp::IdivRaxByFrame:
    case MirOp::MovRegFromFrame:
    case MirOp::MovFrameFromReg:
    case MirOp::AddRaxFromFrame:
    case MirOp::SubRaxFromFrame:
    case MirOp::MovXmm0FromFrame:
    case MirOp::AddXmm0FromFrame:
    case MirOp::SubXmm0FromFrame:
    case MirOp::MovFrameFromXmm0:
    case MirOp::MulXmm0FromFrame:
    case MirOp::DivXmm0FromFrame:
    case MirOp::ComisdXmm0FrameSetb:
    case MirOp::ComisdXmm0FrameSetbe:
    case MirOp::ComisdXmm0FrameSete:
    case MirOp::ComisdXmm0FrameSeta:
    case MirOp::ComisdXmm0FrameSetae:
    case MirOp::ComisdXmm0FrameSetnz:
    case MirOp::MovEaxFromFrame:
    case MirOp::AddEaxFromFrame:
    case MirOp::SubEaxFromFrame:
    case MirOp::MulEaxFromFrame:
    case MirOp::IdivEaxByFrame:
    case MirOp::CmpEaxFrameSetl:
    case MirOp::CmpEaxFrameSetg:
    case MirOp::CmpEaxFrameSete:
    case MirOp::CmpEaxFrameSetne:
    case MirOp::CmpEaxFrameSetle:
    case MirOp::CmpEaxFrameSetge:
    case MirOp::MovSsXmm0FromFrame:
    case MirOp::AddSsXmm0FromFrame:
    case MirOp::SubSsXmm0FromFrame:
    case MirOp::MulSsXmm0FromFrame:
    case MirOp::DivSsXmm0FromFrame:
    case MirOp::MovSsFrameFromXmm0:
    case MirOp::ComissXmm0FrameSetb:
    case MirOp::ComissXmm0FrameSetbe:
    case MirOp::ComissXmm0FrameSete:
    case MirOp::ComissXmm0FrameSeta:
    case MirOp::ComissXmm0FrameSetae:
    case MirOp::ComissXmm0FrameSetnz:
        return true;
    default:
        return false;
    }
}

void mirPrint(const MirBuffer &buf, std::ostream &out, const MirPrintOptions &opts) {
    const size_t n = buf.size();
    // 该 pc 对应的首条 MIR 才在 [pc] 列显示数值
    std::vector<bool> firstPc(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (!buf[i].hasPc())
            continue;
        uint32_t pc = buf[i].pc;
        bool seen   = false;
        for (size_t j = 0; j < i; ++j)
            if (buf[j].hasPc() && buf[j].pc == pc) {
                seen = true;
                break;
            }
        if (!seen)
            firstPc[i] = true;
    }
    uint32_t maxPc = 0;
    for (size_t i = 0; i < n; ++i)
        if (firstPc[i] && buf[i].pc > maxPc)
            maxPc = buf[i].pc;
    int pcWidth = 1;
    for (uint32_t k = maxPc; k >= 10; k /= 10)
        ++pcWidth;
    if (pcWidth < 2)
        pcWidth = 2; // 至少两位，与 idx 对齐
    int idxWidth = 1;
    for (size_t k = (n <= 1u) ? 0u : n - 1; k >= 10; k /= 10)
        ++idxWidth;
    if (idxWidth < 2)
        idxWidth = 2;
    for (size_t i = 0; i < n; ++i) {
        const Mir &m = buf[i];
        out << "  [";
        if (firstPc[i])
            out << std::setw(pcWidth) << m.pc << "]";
        else
            out << std::string(static_cast<size_t>(pcWidth), ' ') << "]";
        out << "[" << std::setw(idxWidth) << i << "]  ";
        out << mirToString(m);
        if (opts.symbolNames && m.op == MirOp::MovRegImm64) {
            auto it = opts.symbolNames->find(m.imm64);
            if (it != opts.symbolNames->end())
                out << "  ; " << it->second;
        }
        if (mirOpUsesFrameDisp(m.op)) {
            if (opts.slotNames) {
                auto it = opts.slotNames->find(m.disp);
                if (it != opts.slotNames->end())
                    out << "  ; " << it->second;
                else
                    out << "  ; slot " << (m.disp / 8);
            } else
                out << "  ; slot " << (m.disp / 8);
        }
        out << "\n";
    }
}

size_t mirSizeBytes(const Mir &m) {
    switch (m.op) {
    case MirOp::MovRegReg:
        return 3;
    case MirOp::MovRegImm32:
        return (m.r0 == 4) ? 6 : 7; // r8d: 41 b8+4; rdx/rcx: 48 c7 c2/c1+4
    case MirOp::MovRegImm64:
        return 10; // rex + b8/b9 + 8
    case MirOp::MovRegFromFrame:
    case MirOp::MovFrameFromReg:
        return fitsDisp8(m.disp) ? 3 : 6;
    case MirOp::MovRaxFromMemAt:
    case MirOp::AddRaxFromMemAt:
    case MirOp::SubRaxFromMemAt:
        return 10 + 3; // mov rbx, imm64 + op [rbx]
    case MirOp::MovXmm0FromMemAt:
    case MirOp::AddXmm0FromMemAt:
    case MirOp::SubXmm0FromMemAt:
        return 10 + 4; // mov rbx, imm64 + xmm op
    case MirOp::MovRaxFromReg:
    case MirOp::MovRegFromRax:
        return (m.r0 == 0) ? 0 : 3;
    case MirOp::AddRaxFromReg:
    case MirOp::SubRaxFromReg:
        return 3;
    case MirOp::AddRaxFromFrame:
    case MirOp::SubRaxFromFrame:
        return fitsDisp8(m.disp) ? 3 : 6;
    case MirOp::MovXmm0FromFrame:
    case MirOp::AddXmm0FromFrame:
    case MirOp::SubXmm0FromFrame:
    case MirOp::MovFrameFromXmm0:
        return fitsDisp8(m.disp) ? 4 : 7;
    case MirOp::MovXmm0FromReg:
        return 5;
    case MirOp::AddXmm0FromReg:
    case MirOp::SubXmm0FromReg:
        return 5 + 4; // movq xmm1, reg + addsd/subsd
    case MirOp::MovRegFromXmm0:
        return 5;
    case MirOp::CmpRaxImm8Setle:
        return 4 + 3 + 4; // cmp + setle + movzx
    case MirOp::CmpRaxFrameSetle:
        return (fitsDisp8(m.disp) ? 3 : 6) + 3 + 4; // cmp + setle + movzx
    case MirOp::CmpRaxMemAtSetle:
        return 10 + 3 + 3 + 4;
    case MirOp::CmpRaxFrameSetl:
    case MirOp::CmpRaxFrameSetg:
    case MirOp::CmpRaxFrameSete:
    case MirOp::CmpRaxFrameSetne:
    case MirOp::CmpRaxFrameSetge:
        return (fitsDisp8(m.disp) ? 3 : 6) + 3 + 4;
    case MirOp::CmpRaxMemAtSetl:
    case MirOp::CmpRaxMemAtSetg:
    case MirOp::CmpRaxMemAtSete:
    case MirOp::CmpRaxMemAtSetne:
    case MirOp::CmpRaxMemAtSetge:
        return 10 + 3 + 3 + 4;
    case MirOp::MulRaxFromReg:
        return 4; // rex.w + 0f af /r
    case MirOp::MulRaxFromFrame:
        return (fitsDisp8(m.disp) ? 4 : 7);
    case MirOp::MulRaxFromMemAt:
        return 10 + 3;
    case MirOp::IdivRaxByReg:
        return 2 + 2; // cqo + idiv
    case MirOp::IdivRaxByFrame:
        return 2 + (fitsDisp8(m.disp) ? 3 : 6);
    case MirOp::IdivRaxByMemAt:
        return 10 + 2 + 3;
    case MirOp::MulXmm0FromReg:
    case MirOp::DivXmm0FromReg:
        return 5 + 4; // movq xmm1, reg + mulsd/divsd
    case MirOp::MulXmm0FromFrame:
    case MirOp::DivXmm0FromFrame:
        return fitsDisp8(m.disp) ? 4 : 7;
    case MirOp::MulXmm0FromMemAt:
    case MirOp::DivXmm0FromMemAt:
        return 10 + 4;
    case MirOp::ComisdXmm0FrameSetb:
    case MirOp::ComisdXmm0FrameSetbe:
    case MirOp::ComisdXmm0FrameSete:
    case MirOp::ComisdXmm0FrameSeta:
    case MirOp::ComisdXmm0FrameSetae:
    case MirOp::ComisdXmm0FrameSetnz:
        return (fitsDisp8(m.disp) ? 4 : 7) + 3 + 4; // comisd + setcc + movzx
    case MirOp::ComisdXmm0MemAtSetb:
    case MirOp::ComisdXmm0MemAtSetbe:
    case MirOp::ComisdXmm0MemAtSete:
    case MirOp::ComisdXmm0MemAtSeta:
    case MirOp::ComisdXmm0MemAtSetae:
    case MirOp::ComisdXmm0MemAtSetnz:
        return 10 + 4 + 3 + 4;
    case MirOp::ComisdXmm0RegSetb:
    case MirOp::ComisdXmm0RegSetbe:
    case MirOp::ComisdXmm0RegSete:
    case MirOp::ComisdXmm0RegSeta:
    case MirOp::ComisdXmm0RegSetae:
    case MirOp::ComisdXmm0RegSetnz:
        return 5 + 4 + 3 + 4;
    case MirOp::MovEaxFromFrame:
    case MirOp::AddEaxFromFrame:
    case MirOp::SubEaxFromFrame:
        return fitsDisp8(m.disp) ? 3 : 6;
    case MirOp::MovEaxFromReg:
    case MirOp::AddEaxFromReg:
    case MirOp::SubEaxFromReg:
        return (m.r0 == 0) ? 0 : 3;
    case MirOp::MovEaxFromMemAt:
    case MirOp::AddEaxFromMemAt:
    case MirOp::SubEaxFromMemAt:
        return 10 + 3;
    case MirOp::MulEaxFromFrame:
        return fitsDisp8(m.disp) ? 4 : 7;
    case MirOp::MulEaxFromReg:
        return 4;
    case MirOp::MulEaxFromMemAt:
        return 10 + 3;
    case MirOp::IdivEaxByFrame:
        return 2 + (fitsDisp8(m.disp) ? 3 : 6);
    case MirOp::IdivEaxByReg:
        return 2 + 2;
    case MirOp::IdivEaxByMemAt:
        return 10 + 2 + 3;
    case MirOp::CmpEaxFrameSetl:
    case MirOp::CmpEaxFrameSetg:
    case MirOp::CmpEaxFrameSete:
    case MirOp::CmpEaxFrameSetne:
    case MirOp::CmpEaxFrameSetle:
    case MirOp::CmpEaxFrameSetge:
        return (fitsDisp8(m.disp) ? 3 : 6) + 3 + 4;
    case MirOp::CmpEaxMemAtSetl:
    case MirOp::CmpEaxMemAtSetg:
    case MirOp::CmpEaxMemAtSete:
    case MirOp::CmpEaxMemAtSetne:
    case MirOp::CmpEaxMemAtSetle:
    case MirOp::CmpEaxMemAtSetge:
        return 10 + 3 + 3 + 4;
    case MirOp::CmpEaxRegSetl:
    case MirOp::CmpEaxRegSetg:
    case MirOp::CmpEaxRegSete:
    case MirOp::CmpEaxRegSetne:
    case MirOp::CmpEaxRegSetle:
    case MirOp::CmpEaxRegSetge:
        return 3 + 3 + 4;
    case MirOp::MovSsXmm0FromFrame:
    case MirOp::AddSsXmm0FromFrame:
    case MirOp::SubSsXmm0FromFrame:
    case MirOp::MulSsXmm0FromFrame:
    case MirOp::DivSsXmm0FromFrame:
    case MirOp::MovSsFrameFromXmm0:
        return fitsDisp8(m.disp) ? 4 : 7;
    case MirOp::MovSsXmm0FromReg:
        return 5;
    case MirOp::AddSsXmm0FromReg:
    case MirOp::SubSsXmm0FromReg:
    case MirOp::MulSsXmm0FromReg:
    case MirOp::DivSsXmm0FromReg:
        return 5 + 4; // movd xmm1, reg + op xmm0, xmm1
    case MirOp::MovSsXmm0FromMemAt:
    case MirOp::AddSsXmm0FromMemAt:
    case MirOp::SubSsXmm0FromMemAt:
    case MirOp::MulSsXmm0FromMemAt:
    case MirOp::DivSsXmm0FromMemAt:
        return 10 + 4;
    case MirOp::MovSsRegFromXmm0:
        return 5;
    case MirOp::ComissXmm0FrameSetb:
    case MirOp::ComissXmm0FrameSetbe:
    case MirOp::ComissXmm0FrameSete:
    case MirOp::ComissXmm0FrameSeta:
    case MirOp::ComissXmm0FrameSetae:
    case MirOp::ComissXmm0FrameSetnz:
        return (fitsDisp8(m.disp) ? 4 : 7) + 3 + 4;
    case MirOp::ComissXmm0MemAtSetb:
    case MirOp::ComissXmm0MemAtSetbe:
    case MirOp::ComissXmm0MemAtSete:
    case MirOp::ComissXmm0MemAtSeta:
    case MirOp::ComissXmm0MemAtSetae:
    case MirOp::ComissXmm0MemAtSetnz:
        return 10 + 4 + 3 + 4;
    case MirOp::ComissXmm0RegSetb:
    case MirOp::ComissXmm0RegSetbe:
    case MirOp::ComissXmm0RegSete:
    case MirOp::ComissXmm0RegSeta:
    case MirOp::ComissXmm0RegSetae:
    case MirOp::ComissXmm0RegSetnz:
        return 5 + 4 + 3 + 4; // movd + comiss + setcc + movzx
    case MirOp::TestRaxRax:
        return 3;
    case MirOp::TestRaxJzRel32:
        return 3 + 6; // test + jz rel32
    case MirOp::CmoveRcxFromRbx:
        return 4;
    case MirOp::JmpRel32:
        return 5;
    case MirOp::JleRel32:
        return 6;
    case MirOp::JmpRel8:
        return 2;
    case MirOp::JleRel8:
        return 2;
    case MirOp::Ret:
        return 1;
    case MirOp::CallRax:
        return 2;
    case MirOp::Nop:
        return 1;
    }
    return 0;
}

} // namespace camel::jit::x64
