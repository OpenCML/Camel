/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
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
    case MirOp::MovRegFromFrame:
    case MirOp::MovFrameFromReg:
    case MirOp::AddRaxFromFrame:
    case MirOp::SubRaxFromFrame:
    case MirOp::MovXmm0FromFrame:
    case MirOp::AddXmm0FromFrame:
    case MirOp::SubXmm0FromFrame:
    case MirOp::MovFrameFromXmm0:
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
