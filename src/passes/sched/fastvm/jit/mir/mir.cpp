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
 * Updated: Mar. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "mir.h"

#include <iomanip>
#include <sstream>
#include <utility>

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
    case MirOp::VLoadFromFrame:
        os << "vload v" << static_cast<int>(m.r0) << ", [rdi+" << m.disp << "]";
        break;
    case MirOp::VStoreToFrame:
        os << "vstore [rdi+" << m.disp << "], v" << static_cast<int>(m.r0);
        break;
    case MirOp::VLoadFromMemAt: {
        std::ostringstream hex;
        hex << "0x" << std::hex << m.imm64;
        os << "vload v" << static_cast<int>(m.r0) << ", [" << hex.str() << "]";
        break;
    }
    case MirOp::VLoadImm32:
        os << "vloadimm32 v" << static_cast<int>(m.r0) << ", " << m.imm32;
        break;
    case MirOp::VLoadImm64:
        os << "vloadimm64 v" << static_cast<int>(m.r0) << ", 0x" << std::hex << m.imm64 << std::dec;
        break;
    case MirOp::VCopy:
        os << "vcopy v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1);
        break;
    case MirOp::VTest:
        os << "vtest v" << static_cast<int>(m.r0);
        break;
    case MirOp::VCmove:
        os << "vcmove v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1);
        break;
    case MirOp::VCmovnz:
        os << "vcmovnz v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1);
        break;
    case MirOp::VMovFromRax:
        os << "vmov v" << static_cast<int>(m.r0) << ", rax";
        break;
    case MirOp::VMovToRax:
        os << "vmov rax, v" << static_cast<int>(m.r0);
        break;
    case MirOp::VRet:
        os << "vret v" << static_cast<int>(m.r0);
        break;
    case MirOp::VAdd:
        os << "vadd v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VSub:
        os << "vsub v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VMul:
        os << "vmul v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VIdiv:
        os << "vidiv v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetL:
        os << "vcmp.lt v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetLE:
        os << "vcmp.le v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetG:
        os << "vcmp.gt v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetGE:
        os << "vcmp.ge v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetE:
        os << "vcmp.eq v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetNE:
        os << "vcmp.ne v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmLoadFromFrame:
        os << "vxmm.load v" << static_cast<int>(m.r0) << ", [rdi+" << m.disp << "]";
        break;
    case MirOp::VXmmStoreToFrame:
        os << "vxmm.store [rdi+" << m.disp << "], v" << static_cast<int>(m.r0);
        break;
    case MirOp::VXmmLoadFromMemAt:
        os << "vxmm.load v" << static_cast<int>(m.r0) << ", [0x" << std::hex << m.imm64 << std::dec
           << "]";
        break;
    case MirOp::VXmmAdd:
        os << "vxmm.add v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmSub:
        os << "vxmm.sub v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmMul:
        os << "vxmm.mul v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmDiv:
        os << "vxmm.div v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmCmpSetB:
        os << "vxmm.cmp.b v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmCmpSetBE:
        os << "vxmm.cmp.be v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmCmpSetE:
        os << "vxmm.cmp.eq v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmCmpSetA:
        os << "vxmm.cmp.a v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmCmpSetAE:
        os << "vxmm.cmp.ae v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmmCmpSetNZ:
        os << "vxmm.cmp.nz v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VAdd32:
        os << "vadd32 v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VSub32:
        os << "vsub32 v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VMul32:
        os << "vmul32 v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VIdiv32:
        os << "vidiv32 v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetL32:
        os << "vcmp32.lt v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetLE32:
        os << "vcmp32.le v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetG32:
        os << "vcmp32.gt v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetGE32:
        os << "vcmp32.ge v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetE32:
        os << "vcmp32.eq v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VCmpSetNE32:
        os << "vcmp32.ne v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32LoadFromFrame:
        os << "vxmm32.load v" << static_cast<int>(m.r0) << ", [rdi+" << m.disp << "]";
        break;
    case MirOp::VXmm32StoreToFrame:
        os << "vxmm32.store [rdi+" << m.disp << "], v" << static_cast<int>(m.r0);
        break;
    case MirOp::VXmm32LoadFromMemAt:
        os << "vxmm32.load v" << static_cast<int>(m.r0) << ", [0x" << std::hex << m.imm64
           << std::dec << "]";
        break;
    case MirOp::VXmm32Add:
        os << "vxmm32.add v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32Sub:
        os << "vxmm32.sub v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32Mul:
        os << "vxmm32.mul v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32Div:
        os << "vxmm32.div v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32CmpSetB:
        os << "vxmm32.cmp.b v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32CmpSetBE:
        os << "vxmm32.cmp.be v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1)
           << ", v" << m.imm32;
        break;
    case MirOp::VXmm32CmpSetE:
        os << "vxmm32.cmp.eq v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1)
           << ", v" << m.imm32;
        break;
    case MirOp::VXmm32CmpSetA:
        os << "vxmm32.cmp.a v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1) << ", v"
           << m.imm32;
        break;
    case MirOp::VXmm32CmpSetAE:
        os << "vxmm32.cmp.ae v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1)
           << ", v" << m.imm32;
        break;
    case MirOp::VXmm32CmpSetNZ:
        os << "vxmm32.cmp.nz v" << static_cast<int>(m.r0) << ", v" << static_cast<int>(m.r1)
           << ", v" << m.imm32;
        break;
    case MirOp::JzRel32:
        os << "jz pc=" << m.imm32;
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
    case MirOp::CallRax:
        os << "call rax";
        break;
    case MirOp::PushRdi:
        os << "push rdi";
        break;
    case MirOp::PopRdi:
        os << "pop rdi";
        break;
    case MirOp::PushRsi:
        os << "push rsi";
        break;
    case MirOp::PopRsi:
        os << "pop rsi";
        break;
    case MirOp::PushRbx:
        os << "push rbx";
        break;
    case MirOp::PopRbx:
        os << "pop rbx";
        break;
    case MirOp::SubRsp8:
        os << "sub rsp, 8";
        break;
    case MirOp::AddRsp8:
        os << "add rsp, 8";
        break;
    case MirOp::Ret:
        os << "ret";
        break;
    case MirOp::DebugTrace:
        os << "debug_trace pc=" << m.pc;
        break;
    case MirOp::Nop:
        os << "nop";
        break;
    }
    return os.str();
}

// 使用 imm64 作为静态地址的 op，可带符号/static slot 注释
static bool mirOpUsesImm64AsAddr(MirOp op) {
    switch (op) {
    case MirOp::MovRegImm64:
    case MirOp::VLoadFromMemAt:
    case MirOp::VXmmLoadFromMemAt:
    case MirOp::VXmm32LoadFromMemAt:
        return true;
    default:
        return false;
    }
}

// 仅用于帧访问的 op 才带 [rdi+disp]，需槽位注释
static bool mirOpUsesFrameDisp(MirOp op) {
    switch (op) {
    case MirOp::VLoadFromFrame:
    case MirOp::VStoreToFrame:
    case MirOp::VXmmLoadFromFrame:
    case MirOp::VXmmStoreToFrame:
    case MirOp::VXmm32LoadFromFrame:
    case MirOp::VXmm32StoreToFrame:
        return true;
    default:
        return false;
    }
}

// 注释内容由 MIR 层决定，格式：frame slot[N]、static slot[-N] 或符号名由 opts 提供
static std::string
formatFrameSlotComment(int disp, const std::unordered_map<int, std::string> *slotNames) {
    if (slotNames) {
        auto it = slotNames->find(disp);
        if (it != slotNames->end())
            return std::string("  ; ") + it->second;
    }
    return "  ; frame slot[" + std::to_string(disp / 8) + "]";
}

static std::string
formatSymbolComment(uint64_t imm64, const std::unordered_map<uint64_t, std::string> *symbolNames) {
    if (!symbolNames)
        return {};
    auto it = symbolNames->find(imm64);
    if (it == symbolNames->end())
        return {};
    return std::string("  ; ") + it->second;
}

// 返回 (行号, 注释) 列表，注释可挂在任意行；行号与 mirToString 展开行一致（0 起）
static std::vector<std::pair<size_t, std::string>>
mirCommentAssignments(const Mir &m, const MirPrintOptions &opts) {
    std::vector<std::pair<size_t, std::string>> out;
    const size_t commentLine = 0u; // 当前所有带注释的 op 其操作数均在首行
    if (mirOpUsesFrameDisp(m.op)) {
        std::string c = formatFrameSlotComment(m.disp, opts.slotNames);
        if (!c.empty())
            out.emplace_back(commentLine, std::move(c));
    }
    if (mirOpUsesImm64AsAddr(m.op)) {
        std::string c = formatSymbolComment(m.imm64, opts.symbolNames);
        if (!c.empty())
            out.emplace_back(commentLine, std::move(c));
    }
    return out;
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
        const Mir &m               = buf[i];
        std::string linePrefixCont = std::string(pcWidth + idxWidth + 6, ' ') + "  ";
        std::string text           = mirToString(m);
        std::vector<std::pair<size_t, std::string>> assignments = mirCommentAssignments(m, opts);
        // 按行收集注释（支持多行时注释在任意行）
        std::vector<std::string> lineComments;
        {
            size_t numLines = 1u;
            for (char ch : text)
                if (ch == '\n')
                    ++numLines;
            lineComments.resize(numLines);
            for (const auto &[lineIdx, comment] : assignments)
                if (lineIdx < numLines)
                    lineComments[lineIdx] = comment;
        }
        size_t pos     = 0;
        size_t lineIdx = 0;
        for (bool first = true;; first = false) {
            size_t next      = text.find('\n', pos);
            bool isLast      = (next == std::string::npos);
            std::string line = isLast ? text.substr(pos) : text.substr(pos, next - pos);
            if (first) {
                out << "  [";
                if (firstPc[i])
                    out << std::setw(pcWidth) << m.pc << "]";
                else
                    out << std::string(static_cast<size_t>(pcWidth), ' ') << "]";
                out << "[" << std::setw(idxWidth) << i << "]  ";
            } else
                out << linePrefixCont;
            out << line;
            if (lineIdx < lineComments.size())
                out << lineComments[lineIdx];
            if (isLast) {
                out << "\n";
                break;
            }
            out << "\n";
            pos = next + 1;
            ++lineIdx;
        }
    }
}

size_t mirSizeBytes(const Mir &m) {
    switch (m.op) {
    case MirOp::MovRegReg:
        return 3;
    case MirOp::MovRegImm32:
        return (m.r0 == 4) ? 6 : 7;
    case MirOp::MovRegImm64:
        return 10;
    case MirOp::CallRax:
        return 2;
    case MirOp::PushRdi:
    case MirOp::PopRdi:
    case MirOp::PushRsi:
    case MirOp::PopRsi:
    case MirOp::PushRbx:
    case MirOp::PopRbx:
        return 1;
    case MirOp::SubRsp8:
    case MirOp::AddRsp8:
        return 4;
    case MirOp::Ret:
        return 1;
    case MirOp::JzRel32:
        return 6;
    case MirOp::JmpRel32:
        return 5;
    case MirOp::JleRel32:
        return 6;
    case MirOp::JmpRel8:
        return 2;
    case MirOp::JleRel8:
        return 2;
    case MirOp::VLoadFromFrame:
    case MirOp::VStoreToFrame:
        return fitsDisp8(m.disp) ? 3 : 6;
    case MirOp::VLoadFromMemAt:
        return 10 + 3;
    case MirOp::VLoadImm32:
        return (m.r0 == 4) ? 6 : 7;
    case MirOp::VLoadImm64:
        return 10;
    case MirOp::VCopy:
        return 3;
    case MirOp::VTest:
        return 3;
    case MirOp::VCmove:
    case MirOp::VCmovnz:
        return 4;
    case MirOp::VMovFromRax:
    case MirOp::VMovToRax:
        return (m.r0 == 0) ? 0 : 3;
    case MirOp::VRet:
        return 1;
    case MirOp::VAdd:
    case MirOp::VSub:
    case MirOp::VMul:
        return 3;
    case MirOp::VIdiv:
        return 2 + 2;
    case MirOp::VCmpSetL:
    case MirOp::VCmpSetLE:
    case MirOp::VCmpSetG:
    case MirOp::VCmpSetGE:
    case MirOp::VCmpSetE:
    case MirOp::VCmpSetNE:
        return (fitsDisp8(m.disp) ? 3 : 6) + 3 + 4;
    case MirOp::VXmmLoadFromFrame:
    case MirOp::VXmmStoreToFrame:
        return fitsDisp8(m.disp) ? 4 : 7;
    case MirOp::VXmmLoadFromMemAt:
        return 10 + 4;
    case MirOp::VXmmAdd:
    case MirOp::VXmmSub:
    case MirOp::VXmmMul:
    case MirOp::VXmmDiv:
        return 5 + 4;
    case MirOp::VXmmCmpSetB:
    case MirOp::VXmmCmpSetBE:
    case MirOp::VXmmCmpSetE:
    case MirOp::VXmmCmpSetA:
    case MirOp::VXmmCmpSetAE:
    case MirOp::VXmmCmpSetNZ:
        return 5 + 4 + 3 + 4;
    case MirOp::VAdd32:
    case MirOp::VSub32:
    case MirOp::VMul32:
        return 3;
    case MirOp::VIdiv32:
        return 2 + 2;
    case MirOp::VCmpSetL32:
    case MirOp::VCmpSetLE32:
    case MirOp::VCmpSetG32:
    case MirOp::VCmpSetGE32:
    case MirOp::VCmpSetE32:
    case MirOp::VCmpSetNE32:
        return (fitsDisp8(m.disp) ? 3 : 6) + 3 + 4;
    case MirOp::VXmm32LoadFromFrame:
    case MirOp::VXmm32StoreToFrame:
        return fitsDisp8(m.disp) ? 4 : 7;
    case MirOp::VXmm32LoadFromMemAt:
        return 10 + 4;
    case MirOp::VXmm32Add:
    case MirOp::VXmm32Sub:
    case MirOp::VXmm32Mul:
    case MirOp::VXmm32Div:
        return 5 + 4;
    case MirOp::VXmm32CmpSetB:
    case MirOp::VXmm32CmpSetBE:
    case MirOp::VXmm32CmpSetE:
    case MirOp::VXmm32CmpSetA:
    case MirOp::VXmm32CmpSetAE:
    case MirOp::VXmm32CmpSetNZ:
        return 5 + 4 + 3 + 4;
    case MirOp::DebugTrace:
        return 80; // 估算：push×15 + sub + mov(pc) + lea + mov(rax,fn) + call + add + pop×15
    case MirOp::Nop:
        return 1;
    }
    return 0;
}

} // namespace camel::jit::x64
