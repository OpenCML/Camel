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
 * Created: Feb. 06, 2026
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace camel::jit::x64 {

class Encoder {
  public:
    explicit Encoder(
        std::vector<uint8_t> &out, std::ostream *asmOut = nullptr, size_t baseAddr = 0,
        bool recordAsm = false)
        : out_(out), asmOut_(asmOut), baseAddr_(baseAddr), recordAsm_(recordAsm) {}

    size_t size() const { return out_.size(); }
    size_t here() const { return out_.size(); }

    void emitByte(uint8_t b) { out_.push_back(b); }
    // 缓冲 asm 行 (地址, 指令文本)，flush 时按实际最大地址宽度右对齐；始终记录以便取指令边界
    void asmLine(const std::string &s) {
        if (!recordAsm_)
            return;
        asmLines_.emplace_back(baseAddr_ + here(), s);
    }
    // 用指令起始偏移记录 asm 行（用于跳转等，保证 [addr] 与下一指令间隔 = 本条指令长度）
    void asmLineAt(size_t instrStart, const std::string &s) {
        if (!recordAsm_)
            return;
        asmLines_.emplace_back(baseAddr_ + instrStart, s);
    }
    size_t getAsmLineCount() const { return asmLines_.size(); }
    // 供 bindump 等按指令分解机器码：(baseAddr 相对) 起始偏移、长度、汇编文本
    const std::vector<std::pair<size_t, std::string>> &getAsmLines() const { return asmLines_; }
    // 将缓冲中第 index 行的 rel32 显示改为 rel（用于 jz/jmp/jle 修补后）；rel=0 时注释标为 (next)
    // 避免误解为死循环
    void setAsmLineRel(size_t index, int32_t rel) {
        if (index >= asmLines_.size())
            return;
        std::string &line = asmLines_[index].second;
        size_t pos        = line.find(" .+");
        if (pos == std::string::npos)
            pos = line.find(" .-");
        if (pos == std::string::npos)
            return;
        size_t end = line.find("  ;", pos);
        if (end == std::string::npos)
            end = line.size();
        std::string repl = (rel >= 0 ? " .+" : " .") + std::to_string(rel);
        line.replace(pos, end - pos, repl);
    }
    void flushAsmTo(std::ostream *out) {
        if (!out || asmLines_.empty())
            return;
        size_t maxAddr = 0;
        for (const auto &p : asmLines_)
            if (p.first > maxAddr)
                maxAddr = p.first;
        int width = static_cast<int>(std::to_string(maxAddr).size());
        for (const auto &p : asmLines_)
            *out << "  [" << std::setw(width) << std::right << p.first << "]  " << p.second << "\n";
    }

    void emitBytes(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes)
            out_.push_back(b);
    }

    static bool fitsDisp8(int disp) { return disp >= -128 && disp <= 127; }
    void emitDisp32(int disp) {
        emitBytes({
            static_cast<uint8_t>(disp & 0xff),
            static_cast<uint8_t>((disp >> 8) & 0xff),
            static_cast<uint8_t>((disp >> 16) & 0xff),
            static_cast<uint8_t>((disp >> 24) & 0xff),
        });
    }

    // REX.W prefix (48), REX.WR (4C) for Reg field extended, REX.WB (49) for R/M field extended
    void rexW() { emitByte(0x48); }
    void rexWR() { emitByte(0x4C); }
    void rexWB() { emitByte(0x49); }

    // --- 标准槽模型：每槽一字（8 字节），以下所有 frame/静态区 访问均为 64 位（r64/m64 或 xmm
    // 双精度）---

    // mov rax, [rdi + disp] (disp8 或 disp32)，64 位加载一槽
    void movRaxFromFrame(int disp) {
        size_t at = here();
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x8b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x8b, 0x87});
            emitDisp32(disp);
        }
        asmLineAt(at, "mov rax, [rdi+" + std::to_string(disp) + "]");
    }

    static const char *regName(uint8_t r) {
        static const char *n[] = {"rax", "rcx", "rdx", "rbx", "r8", "r9", "r10", "r11"};
        if (r <= 7)
            return n[r];
        if (r == 8)
            return "rdi";
        if (r == 9)
            return "rsi";
        return "?";
    }

    // mov reg, [rdi + disp] (reg: 0=rax..7=r11)
    void movRegFromFrame(uint8_t reg, int disp) {
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t mod    = fitsDisp8(disp) ? 0x40 : 0x80;
        uint8_t modrm  = static_cast<uint8_t>(mod | ((regEnc & 3) << 3) | 7);
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(mod | ((regEnc - 4) << 3) | 7);
        }
        emitByte(0x8b);
        emitByte(modrm);
        if (fitsDisp8(disp))
            emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitDisp32(disp);
        asmLineAt(at, std::string("mov ") + regName(reg) + ", [rdi+" + std::to_string(disp) + "]");
    }

    // mov [rdi + disp], r64 (reg: 0=rax..7=r11)
    void movToFrame(int disp, uint8_t reg) {
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t mod    = fitsDisp8(disp) ? 0x40 : 0x80;
        uint8_t modrm  = static_cast<uint8_t>(mod | ((regEnc & 3) << 3) | 7);
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(mod | ((regEnc - 4) << 3) | 7);
        }
        emitByte(0x89);
        emitByte(modrm);
        if (fitsDisp8(disp))
            emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitDisp32(disp);
        asmLineAt(at, std::string("mov [rdi+") + std::to_string(disp) + "], " + regName(reg));
    }

    // mov [rdi + disp], rax，64 位写回一槽
    void movFrameFromRax(int disp) {
        size_t at = here();
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x89, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x89, 0x87});
            emitDisp32(disp);
        }
        asmLineAt(at, "mov [rdi+" + std::to_string(disp) + "], rax");
    }

    // mov reg, rax (reg: 0=rax no-op, 1..7=rcx..r11). 89 /r: r/m=dest, reg=rax
    void movRegFromRax(uint8_t reg) {
        if (reg == 0)
            return;
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWB(); // REX.B for R/M field (dest)
            modrm = static_cast<uint8_t>(0xC0 | (regEnc - 4));
        }
        emitBytes({0x89, modrm});
        asmLineAt(at, std::string("mov ") + regName(reg) + ", rax");
    }

    // mov rax, reg (reg: 1..7=rcx..r11). 89 /r: r/m=rax, reg=src
    void movRaxFromReg(uint8_t reg) {
        if (reg == 0)
            return;
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR(); // REX.R for Reg field (src)
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x89, modrm});
        asmLineAt(at, std::string("mov rax, ") + regName(reg));
    }

    // add rax, reg. 01 /r: r/m=rax, reg=src
    void addRaxFromReg(uint8_t reg) {
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x01, modrm});
        asmLineAt(at, std::string("add rax, ") + regName(reg));
    }

    // sub rax, reg. 29 /r: r/m=rax, reg=src
    void subRaxFromReg(uint8_t reg) {
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x29, modrm});
        asmLineAt(at, std::string("sub rax, ") + regName(reg));
    }

    // add rax, [rdi + disp]
    void addRaxFromFrame(int disp) {
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x03, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x03, 0x87});
            emitDisp32(disp);
        }
        asmLine("add rax, [rdi+" + std::to_string(disp) + "]");
    }

    // sub rax, [rdi + disp]
    void subRaxFromFrame(int disp) {
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x2b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x2b, 0x87});
            emitDisp32(disp);
        }
        asmLine("sub rax, [rdi+" + std::to_string(disp) + "]");
    }

    // 从编译期常量地址加载/运算（用于静态区 slot，用 rbx 作临时）
    void movRaxFromMemAt(uint64_t addr) {
        size_t at1 = here();
        emitByte(0x48);
        emitByte(0xbb); // mov rbx, imm64
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLineAt(at1, h.str());
        size_t at2 = here();
        rexW();
        emitBytes({0x8b, 0x03}); // mov rax, [rbx]
        asmLineAt(at2, "mov rax, [rbx]");
    }
    // 从编译期常量地址加载到任意 reg，不经过 rax（避免覆盖左操作数）
    void movRegFromMemAt(uint8_t reg, uint64_t addr) {
        if (reg == 0) {
            movRaxFromMemAt(addr);
            return;
        }
        size_t at1 = here();
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLineAt(at1, h.str());
        size_t at2     = here();
        uint8_t regEnc = (reg <= 3) ? reg : (reg >= 4 && reg <= 7) ? (reg - 4) : (reg == 8) ? 7 : 6;
        uint8_t rex    = 0x48;
        if (reg >= 4 && reg <= 7)
            rex |= 4;
        emitByte(rex);
        emitByte(0x8b);
        emitByte(0x03 | (regEnc << 3));
        asmLineAt(at2, std::string("mov ") + regName(reg) + ", [rbx]");
    }
    void subRaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        rexW();
        emitBytes({0x2b, 0x03}); // sub rax, [rbx]
        asmLine("sub rax, [rbx]");
    }
    void addRaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        rexW();
        emitBytes({0x03, 0x03}); // add rax, [rbx]
        asmLine("add rax, [rbx]");
    }

    // double (float64) 算术：用 xmm0，一槽 8 字节；与 LADD/LSUB 槽布局一致
    void movXmm0FromFrame(int disp) {
        emitBytes({0xf2, 0x0f, 0x10});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("movsd xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void addXmm0FromFrame(int disp) {
        emitBytes({0xf2, 0x0f, 0x58});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("addsd xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void subXmm0FromFrame(int disp) {
        emitBytes({0xf2, 0x0f, 0x5c});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("subsd xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void movFrameFromXmm0(int disp) {
        emitBytes({0xf2, 0x0f, 0x11});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("movsd [rdi+" + std::to_string(disp) + "], xmm0");
    }
    void movXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0xf2, 0x0f, 0x10, 0x03});
        asmLine("movsd xmm0, [rbx]");
    }
    void addXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0xf2, 0x0f, 0x58, 0x03});
        asmLine("addsd xmm0, [rbx]");
    }
    void subXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0xf2, 0x0f, 0x5c, 0x03});
        asmLine("subsd xmm0, [rbx]");
    }
    void mulXmm0FromFrame(int disp) {
        emitBytes({0xf2, 0x0f, 0x59});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("mulsd xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void mulXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC8 | (regEnc & 7)); // movq xmm1, r64
        if (regEnc < 4)
            emitBytes({0x66, 0x48, 0x0f, 0x6e, modrm});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, modrm});
        asmLine(std::string("movq xmm1, ") + regName(reg));
        emitBytes({0xf2, 0x0f, 0x59, 0xc1});
        asmLine("mulsd xmm0, xmm1");
    }
    void mulXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0xf2, 0x0f, 0x59, 0x03});
        asmLine("mulsd xmm0, [rbx]");
    }
    void divXmm0FromFrame(int disp) {
        emitBytes({0xf2, 0x0f, 0x5e});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("divsd xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void divXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC8 | (regEnc & 7));
        if (regEnc < 4)
            emitBytes({0x66, 0x48, 0x0f, 0x6e, modrm});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, modrm});
        asmLine(std::string("movq xmm1, ") + regName(reg));
        emitBytes({0xf2, 0x0f, 0x5e, 0xc1});
        asmLine("divsd xmm0, xmm1");
    }
    void divXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0xf2, 0x0f, 0x5e, 0x03});
        asmLine("divsd xmm0, [rbx]");
    }
    // comisd xmm0, op; setcc al; movzx rax, al（D* 比较，op 在 frame/memat/reg）
    void comisdXmm0WithFrame(int disp) {
        emitBytes({0x66, 0x0f, 0x2f});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("comisd xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void comisdXmm0WithMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0x66, 0x0f, 0x2f, 0x03});
        asmLine("comisd xmm0, [rbx]");
    }
    void comisdXmm0WithReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC8 | (regEnc & 7));
        if (regEnc < 4)
            emitBytes({0x66, 0x48, 0x0f, 0x6e, modrm});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, modrm});
        asmLine(std::string("movq xmm1, ") + regName(reg));
        emitBytes({0x66, 0x0f, 0x2f, 0xc1});
        asmLine("comisd xmm0, xmm1");
    }
    void setbAlMovzxRax() {
        emitBytes({0x0f, 0x92, 0xc0});
        asmLine("setb al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void setbeAlMovzxRax() {
        emitBytes({0x0f, 0x96, 0xc0});
        asmLine("setbe al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void setzAlMovzxRax() {
        emitBytes({0x0f, 0x94, 0xc0});
        asmLine("sete al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void setaAlMovzxRax() {
        emitBytes({0x0f, 0x97, 0xc0});
        asmLine("seta al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void setaeAlMovzxRax() {
        emitBytes({0x0f, 0x93, 0xc0});
        asmLine("setae al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void setnzAlMovzxRax() {
        emitBytes({0x0f, 0x95, 0xc0});
        asmLine("setne al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }

    // --- 32 位整型 (I*)：无 REX.W，操作数 32 位，结果零扩展到 rax ---
    void movEaxFromFrame(int disp) {
        if (fitsDisp8(disp)) {
            emitBytes({0x8b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x8b, 0x87});
            emitDisp32(disp);
        }
        asmLine("mov eax, [rdi+" + std::to_string(disp) + "]");
    }
    void movEaxFromReg(uint8_t reg) {
        if (reg == 0)
            return;
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc >= 4) {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x8b, modrm});
        asmLine(std::string("mov eax, ") + regName(reg));
    }
    // mov reg, eax（32 位，零扩展到 64），用于 VAdd32 等结果写回
    void movRegFromEax(uint8_t reg) {
        if (reg == 0)
            return;
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc >= 4) {
            rexWB();
            modrm = static_cast<uint8_t>(0xC0 | (regEnc - 4));
        }
        emitBytes({0x89, modrm});
        asmLine(std::string("mov ") + regName(reg) + ", eax");
    }
    void movEaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0x8b, 0x03});
        asmLine("mov eax, [rbx]");
    }
    void addEaxFromFrame(int disp) {
        if (fitsDisp8(disp))
            emitBytes({0x03, 0x47, static_cast<uint8_t>(disp & 0xff)});
        else {
            emitBytes({0x03, 0x87});
            emitDisp32(disp);
        }
        asmLine("add eax, [rdi+" + std::to_string(disp) + "]");
    }
    void addEaxFromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc >= 4) {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x01, modrm});
        asmLine(std::string("add eax, ") + regName(reg));
    }
    void addEaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0x03, 0x03});
        asmLine("add eax, [rbx]");
    }
    void subEaxFromFrame(int disp) {
        if (fitsDisp8(disp))
            emitBytes({0x2b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        else {
            emitBytes({0x2b, 0x87});
            emitDisp32(disp);
        }
        asmLine("sub eax, [rdi+" + std::to_string(disp) + "]");
    }
    void subEaxFromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc >= 4) {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x29, modrm});
        asmLine(std::string("sub eax, ") + regName(reg));
    }
    void subEaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0x2b, 0x03});
        asmLine("sub eax, [rbx]");
    }
    void mulEaxFromFrame(int disp) {
        if (fitsDisp8(disp))
            emitBytes({0x0f, 0xaf, 0x47, static_cast<uint8_t>(disp & 0xff)});
        else {
            emitBytes({0x0f, 0xaf, 0x87});
            emitDisp32(disp);
        }
        asmLine("imul eax, [rdi+" + std::to_string(disp) + "]");
    }
    void mulEaxFromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc >= 4) {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x0f, 0xaf, modrm});
        asmLine(std::string("imul eax, ") + regName(reg));
    }
    void mulEaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0x0f, 0xaf, 0x03});
        asmLine("imul eax, [rbx]");
    }
    void idivEaxByFrame(int disp) {
        emitByte(0x99); // cdq
        asmLine("cdq");
        if (fitsDisp8(disp))
            emitBytes({0xf7, 0x7f, static_cast<uint8_t>(disp & 0xff)});
        else {
            emitBytes({0xf7, 0xbf});
            emitDisp32(disp);
        }
        asmLine("idiv dword [rdi+" + std::to_string(disp) + "]");
    }
    void idivEaxByReg(uint8_t reg) {
        emitByte(0x99);
        asmLine("cdq");
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xF8 | (regEnc & 7));
        if (regEnc >= 4) {
            rexWR();
            modrm = static_cast<uint8_t>(0xF8 | ((regEnc - 4) << 3));
        }
        emitBytes({0xf7, modrm});
        asmLine(std::string("idiv ") + regName(reg));
    }
    void idivEaxByMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitByte(0x99);
        asmLine("cdq");
        emitBytes({0xf7, 0x3b});
        asmLine("idiv dword [rbx]");
    }
    void cmpEaxWithFrame(int disp) {
        if (fitsDisp8(disp))
            emitBytes({0x3b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        else {
            emitBytes({0x3b, 0x87});
            emitDisp32(disp);
        }
        asmLine("cmp eax, [rdi+" + std::to_string(disp) + "]");
    }
    void cmpEaxWithMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0x3b, 0x03});
        asmLine("cmp eax, [rbx]");
    }
    void cmpEaxWithReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc >= 4) {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x3b, modrm});
        asmLine(std::string("cmp eax, ") + regName(reg));
    }
    void cmpEaxFrameSetl(int disp) {
        cmpEaxWithFrame(disp);
        emitBytes({0x0f, 0x9c, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxFrameSetg(int disp) {
        cmpEaxWithFrame(disp);
        emitBytes({0x0f, 0x9f, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxFrameSete(int disp) {
        cmpEaxWithFrame(disp);
        emitBytes({0x0f, 0x94, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxFrameSetne(int disp) {
        cmpEaxWithFrame(disp);
        emitBytes({0x0f, 0x95, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxFrameSetle(int disp) {
        cmpEaxWithFrame(disp);
        emitBytes({0x0f, 0x9e, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxFrameSetge(int disp) {
        cmpEaxWithFrame(disp);
        emitBytes({0x0f, 0x9d, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxMemAtSetl(uint64_t addr) {
        cmpEaxWithMemAt(addr);
        emitBytes({0x0f, 0x9c, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxMemAtSetg(uint64_t addr) {
        cmpEaxWithMemAt(addr);
        emitBytes({0x0f, 0x9f, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxMemAtSete(uint64_t addr) {
        cmpEaxWithMemAt(addr);
        emitBytes({0x0f, 0x94, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxMemAtSetne(uint64_t addr) {
        cmpEaxWithMemAt(addr);
        emitBytes({0x0f, 0x95, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxMemAtSetle(uint64_t addr) {
        cmpEaxWithMemAt(addr);
        emitBytes({0x0f, 0x9e, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxMemAtSetge(uint64_t addr) {
        cmpEaxWithMemAt(addr);
        emitBytes({0x0f, 0x9d, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxRegSetl(uint8_t reg) {
        cmpEaxWithReg(reg);
        emitBytes({0x0f, 0x9c, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxRegSetg(uint8_t reg) {
        cmpEaxWithReg(reg);
        emitBytes({0x0f, 0x9f, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxRegSete(uint8_t reg) {
        cmpEaxWithReg(reg);
        emitBytes({0x0f, 0x94, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxRegSetne(uint8_t reg) {
        cmpEaxWithReg(reg);
        emitBytes({0x0f, 0x95, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxRegSetle(uint8_t reg) {
        cmpEaxWithReg(reg);
        emitBytes({0x0f, 0x9e, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }
    void cmpEaxRegSetge(uint8_t reg) {
        cmpEaxWithReg(reg);
        emitBytes({0x0f, 0x9d, 0xc0});
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
    }

    // --- 32 位浮点 (F*)：movss/addss/subss/mulss/divss/comiss，一槽仍 8 字节，低 4 字节为 float
    // ---
    void movSsXmm0FromFrame(int disp) {
        emitBytes({0xf3, 0x0f, 0x10});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("movss xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void movSsXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x6e, modrm});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, static_cast<uint8_t>(0xC0 | (regEnc - 4))});
        asmLine(std::string("movd xmm0, ") + regName(reg));
    }
    void movSsXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0xf3, 0x0f, 0x10, 0x03});
        asmLine("movss xmm0, [rbx]");
    }
    void addSsXmm0FromFrame(int disp) {
        emitBytes({0xf3, 0x0f, 0x58});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("addss xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void addSsXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | regEnc)});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | (regEnc - 4))});
        asmLine(std::string("movd xmm1, ") + regName(reg));
        emitBytes({0xf3, 0x0f, 0x58, 0xc1});
        asmLine("addss xmm0, xmm1");
    }
    void addSsXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0xf3, 0x0f, 0x58, 0x03});
        asmLine("addss xmm0, [rbx]");
    }
    void subSsXmm0FromFrame(int disp) {
        emitBytes({0xf3, 0x0f, 0x5c});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("subss xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void subSsXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | regEnc)});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | (regEnc - 4))});
        asmLine(std::string("movd xmm1, ") + regName(reg));
        emitBytes({0xf3, 0x0f, 0x5c, 0xc1});
        asmLine("subss xmm0, xmm1");
    }
    void subSsXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0xf3, 0x0f, 0x5c, 0x03});
        asmLine("subss xmm0, [rbx]");
    }
    void mulSsXmm0FromFrame(int disp) {
        emitBytes({0xf3, 0x0f, 0x59});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("mulss xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void mulSsXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | regEnc)});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | (regEnc - 4))});
        asmLine(std::string("movd xmm1, ") + regName(reg));
        emitBytes({0xf3, 0x0f, 0x59, 0xc1});
        asmLine("mulss xmm0, xmm1");
    }
    void mulSsXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0xf3, 0x0f, 0x59, 0x03});
        asmLine("mulss xmm0, [rbx]");
    }
    void divSsXmm0FromFrame(int disp) {
        emitBytes({0xf3, 0x0f, 0x5e});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("divss xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void divSsXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | regEnc)});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | (regEnc - 4))});
        asmLine(std::string("movd xmm1, ") + regName(reg));
        emitBytes({0xf3, 0x0f, 0x5e, 0xc1});
        asmLine("divss xmm0, xmm1");
    }
    void divSsXmm0FromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0xf3, 0x0f, 0x5e, 0x03});
        asmLine("divss xmm0, [rbx]");
    }
    void movSsFrameFromXmm0(int disp) {
        emitBytes({0xf3, 0x0f, 0x11});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("movss [rdi+" + std::to_string(disp) + "], xmm0");
    }
    void movSsRegFromXmm0(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x7e, modrm});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x7e, static_cast<uint8_t>(0xC0 | (regEnc - 4))});
        asmLine(std::string("movd ") + regName(reg) + ", xmm0");
    }
    void comissXmm0WithFrame(int disp) {
        emitBytes({0x0f, 0x2f});
        if (fitsDisp8(disp))
            emitByte(0x47), emitByte(static_cast<uint8_t>(disp & 0xff));
        else
            emitByte(0x87), emitDisp32(disp);
        asmLine("comiss xmm0, [rdi+" + std::to_string(disp) + "]");
    }
    void comissXmm0WithMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        asmLine("mov rbx, 0x" + std::to_string(addr));
        emitBytes({0x0f, 0x2f, 0x03});
        asmLine("comiss xmm0, [rbx]");
    }
    void comissXmm0WithReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        if (regEnc < 4)
            emitBytes({0x66, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | regEnc)});
        else
            emitBytes({0x66, 0x49, 0x0f, 0x6e, static_cast<uint8_t>(0xC8 | (regEnc - 4))});
        asmLine(std::string("movd xmm1, ") + regName(reg));
        emitBytes({0x0f, 0x2f, 0xc1});
        asmLine("comiss xmm0, xmm1");
    }

    // double 与 GPR 互转（reg 0..7 = rax..r11，与 regName 一致）
    void movXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc < 4) {
            emitBytes({0x66, 0x48, 0x0f, 0x6e, modrm});
        } else {
            emitBytes({0x66, 0x49, 0x0f, 0x6e, modrm});
        }
        asmLine(std::string("movq xmm0, ") + regName(reg));
    }
    void addXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC8 | (regEnc & 7)); // xmm1 = dest of movq
        if (regEnc < 4) {
            emitBytes({0x66, 0x48, 0x0f, 0x6e, modrm});
        } else {
            emitBytes({0x66, 0x49, 0x0f, 0x6e, modrm});
        }
        asmLine(std::string("movq xmm1, ") + regName(reg));
        emitBytes({0xf2, 0x0f, 0x58, 0xc1});
        asmLine("addsd xmm0, xmm1");
    }
    void subXmm0FromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC8 | (regEnc & 7));
        if (regEnc < 4) {
            emitBytes({0x66, 0x48, 0x0f, 0x6e, modrm});
        } else {
            emitBytes({0x66, 0x49, 0x0f, 0x6e, modrm});
        }
        asmLine(std::string("movq xmm1, ") + regName(reg));
        emitBytes({0xf2, 0x0f, 0x5c, 0xc1});
        asmLine("subsd xmm0, xmm1");
    }
    void movRegFromXmm0(uint8_t reg) {
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc < 4) {
            emitBytes({0x66, 0x48, 0x0f, 0x7e, modrm});
        } else {
            emitBytes({0x66, 0x49, 0x0f, 0x7e, modrm});
        }
        asmLineAt(at, std::string("movq ") + regName(reg) + ", xmm0");
    }

    // cmp [rdi + disp8], imm32 (signed)，64 位比较一槽（REX.W）
    void cmpFrameWithImm(int disp, int32_t imm) {
        rexW();
        emitBytes({0x81, 0x7f, static_cast<uint8_t>(disp & 0xff)});
        emitBytes({
            static_cast<uint8_t>(imm & 0xff),
            static_cast<uint8_t>((imm >> 8) & 0xff),
            static_cast<uint8_t>((imm >> 16) & 0xff),
            static_cast<uint8_t>((imm >> 24) & 0xff),
        });
        asmLine("cmp [rdi+" + std::to_string(disp) + "], " + std::to_string(imm));
    }

    // cmp rax, reg（64 位，用于 VCmpSet* 两操作数均在 reg）
    void cmpRaxWithReg(uint8_t reg) {
        size_t at      = here();
        uint8_t regEnc = reg & 7;
        uint8_t modrm;
        if (regEnc < 4) {
            rexW();
            modrm = static_cast<uint8_t>(0xC0 | regEnc);
        } else {
            rexWB();
            modrm = static_cast<uint8_t>(0xC0 | (regEnc - 4));
        }
        emitBytes({0x3b, modrm});
        asmLineAt(at, std::string("cmp rax, ") + regName(reg));
    }

    void setlAlMovzxRax() {
        size_t at1 = here();
        emitBytes({0x0f, 0x9c, 0xc0});
        asmLineAt(at1, "setl al");
        size_t at2 = here();
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLineAt(at2, "movzx rax, al");
    }
    void setleAlMovzxRax() {
        size_t at1 = here();
        emitBytes({0x0f, 0x9e, 0xc0});
        asmLineAt(at1, "setle al");
        size_t at2 = here();
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLineAt(at2, "movzx rax, al");
    }
    void setgAlMovzxRax() {
        size_t at1 = here();
        emitBytes({0x0f, 0x9f, 0xc0});
        asmLineAt(at1, "setg al");
        size_t at2 = here();
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLineAt(at2, "movzx rax, al");
    }
    void setgeAlMovzxRax() {
        size_t at1 = here();
        emitBytes({0x0f, 0x9d, 0xc0});
        asmLineAt(at1, "setge al");
        size_t at2 = here();
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLineAt(at2, "movzx rax, al");
    }

    // cmp rax, [rdi + disp] 64 位比较一槽（disp8 或 disp32）
    void cmpRaxWithFrame(int disp) {
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x3b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x3b, 0x87});
            emitDisp32(disp);
        }
        asmLine("cmp rax, [rdi+" + std::to_string(disp) + "]");
    }

    // cmp rax, [rdi+disp]; setle al; movzx rax, al（LLE 右操作数在 frame）
    void cmpRaxFrameSetle(int disp) {
        cmpRaxWithFrame(disp);
        emitBytes({0x0f, 0x9e, 0xc0}); // setle al
        asmLine("setle al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0}); // movzx rax, al
        asmLine("movzx rax, al");
    }

    // mov rbx, addr; cmp rax, [rbx]; setle al; movzx rax, al（LLE 右操作数在静态区）
    void cmpRaxMemAtSetle(uint64_t addr) {
        cmpRaxWithMemAt(addr);
        emitBytes({0x0f, 0x9e, 0xc0}); // setle al
        asmLine("setle al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0}); // movzx rax, al
        asmLine("movzx rax, al");
    }

    // cmp rax, [addr]; setcc al; movzx（静态区右操作数），setcc: 0x9c=setl 0x9f=setg 0x94=sete
    // 0x95=setne 0x9d=setge
    void cmpRaxWithMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        rexW();
        emitBytes({0x3b, 0x03}); // cmp rax, [rbx]
        asmLine("cmp rax, [rbx]");
    }
    void cmpRaxFrameSetl(int disp) {
        cmpRaxWithFrame(disp);
        emitBytes({0x0f, 0x9c, 0xc0});
        asmLine("setl al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxMemAtSetl(uint64_t addr) {
        cmpRaxWithMemAt(addr);
        emitBytes({0x0f, 0x9c, 0xc0});
        asmLine("setl al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxFrameSetg(int disp) {
        cmpRaxWithFrame(disp);
        emitBytes({0x0f, 0x9f, 0xc0});
        asmLine("setg al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxMemAtSetg(uint64_t addr) {
        cmpRaxWithMemAt(addr);
        emitBytes({0x0f, 0x9f, 0xc0});
        asmLine("setg al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxFrameSete(int disp) {
        cmpRaxWithFrame(disp);
        emitBytes({0x0f, 0x94, 0xc0});
        asmLine("sete al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxMemAtSete(uint64_t addr) {
        cmpRaxWithMemAt(addr);
        emitBytes({0x0f, 0x94, 0xc0});
        asmLine("sete al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxFrameSetne(int disp) {
        cmpRaxWithFrame(disp);
        emitBytes({0x0f, 0x95, 0xc0});
        asmLine("setne al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxMemAtSetne(uint64_t addr) {
        cmpRaxWithMemAt(addr);
        emitBytes({0x0f, 0x95, 0xc0});
        asmLine("setne al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxFrameSetge(int disp) {
        cmpRaxWithFrame(disp);
        emitBytes({0x0f, 0x9d, 0xc0});
        asmLine("setge al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }
    void cmpRaxMemAtSetge(uint64_t addr) {
        cmpRaxWithMemAt(addr);
        emitBytes({0x0f, 0x9d, 0xc0});
        asmLine("setge al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0});
        asmLine("movzx rax, al");
    }

    // imul rax, r/m64（LMUL）
    void mulRaxFromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x0f, 0xaf, modrm});
        asmLine(std::string("imul rax, ") + regName(reg));
    }
    void mulRaxFromFrame(int disp) {
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x0f, 0xaf, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x0f, 0xaf, 0x87});
            emitDisp32(disp);
        }
        asmLine("imul rax, [rdi+" + std::to_string(disp) + "]");
    }
    void mulRaxFromMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        rexW();
        emitBytes({0x0f, 0xaf, 0x03}); // imul rax, [rbx]
        asmLine("imul rax, [rbx]");
    }

    // cqo; idiv r/m64（LDIV，被除数已在 rax）
    void idivRaxByReg(uint8_t reg) {
        emitBytes({0x48, 0x99}); // cqo
        asmLine("cqo");
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xF8 | (regEnc & 7));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0xF8 | ((regEnc - 4) << 3));
        }
        emitBytes({0xf7, modrm});
        asmLine(std::string("idiv ") + regName(reg));
    }
    void idivRaxByFrame(int disp) {
        emitBytes({0x48, 0x99});
        asmLine("cqo");
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0xf7, 0x7f, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0xf7, 0xbf});
            emitDisp32(disp);
        }
        asmLine("idiv qword [rdi+" + std::to_string(disp) + "]");
    }
    void idivRaxByMemAt(uint64_t addr) {
        emitByte(0x48);
        emitByte(0xbb);
        emitBytes({
            static_cast<uint8_t>(addr & 0xff),
            static_cast<uint8_t>((addr >> 8) & 0xff),
            static_cast<uint8_t>((addr >> 16) & 0xff),
            static_cast<uint8_t>((addr >> 24) & 0xff),
            static_cast<uint8_t>((addr >> 32) & 0xff),
            static_cast<uint8_t>((addr >> 40) & 0xff),
            static_cast<uint8_t>((addr >> 48) & 0xff),
            static_cast<uint8_t>((addr >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov rbx, 0x" << std::hex << addr;
        asmLine(h.str());
        emitBytes({0x48, 0x99});
        asmLine("cqo");
        rexW();
        emitBytes({0xf7, 0x3b}); // idiv [rbx]
        asmLine("idiv qword [rbx]");
    }

    // jmp rel32 (到 offset)
    void jmpRel32(int32_t rel) {
        size_t at = here();
        emitByte(0xe9);
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLineAt(at, "jmp .+" + std::to_string(rel) + "  ; rel32");
    }

    // jle rel32
    void jleRel32(int32_t rel) {
        size_t at = here();
        emitBytes({0x0f, 0x8e});
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLineAt(at, "jle .+" + std::to_string(rel) + "  ; rel32");
    }

    // jmp rel8 (短跳转, -128..127)
    void jmpRel8(int8_t rel) {
        emitByte(0xeb);
        emitByte(static_cast<uint8_t>(rel & 0xff));
        asmLine("jmp .+" + std::to_string(static_cast<int>(rel)) + "  ; rel8");
    }

    // jle rel8
    void jleRel8(int8_t rel) {
        emitBytes({0x7e, static_cast<uint8_t>(rel & 0xff)});
        asmLine("jle .+" + std::to_string(static_cast<int>(rel)) + "  ; rel8");
    }

    // ret
    void ret() {
        size_t at = here();
#if defined(_WIN32) || defined(_WIN64)
        // Win64: JIT 作为普通函数被 C++ 调用，必须在函数返回前恢复非易失寄存器。
        // prologueWin64 额外 sub 了 8 字节以维持函数体内 RSP=8(mod16)，这里对应恢复。
        emitBytes({0x48, 0x83, 0xc4, 0x08}); // add rsp, 8
        asmLineAt(at, "add rsp, 8  ; win64 align pad");
        at = here();
        emitByte(0x5b); // pop rbx
        asmLineAt(at, "pop rbx");
        at = here();
        emitByte(0x5e); // pop rsi
        asmLineAt(at, "pop rsi");
        at = here();
        emitByte(0x5f); // pop rdi
        asmLineAt(at, "pop rdi");
        at = here();
#endif
        emitByte(0xc3);
        asmLineAt(at, "ret");
    }

    void jmpRax() {
        size_t at = here();
        emitBytes({0xff, 0xe0});
        asmLineAt(at, "jmp rax");
    }

    void tailJmpRaxWin64() {
#if defined(_WIN32) || defined(_WIN64)
        size_t at = here();
        emitBytes({0x48, 0x83, 0xc4, 0x08}); // add rsp, 8
        asmLineAt(at, "add rsp, 8  ; win64 align pad");
        at = here();
        emitByte(0x5b); // pop rbx
        asmLineAt(at, "pop rbx");
        at = here();
        emitByte(0x5e); // pop rsi
        asmLineAt(at, "pop rsi");
        at = here();
        emitByte(0x5f); // pop rdi
        asmLineAt(at, "pop rdi");
#endif
        jmpRax();
    }

    // push rdi / pop rdi：trampoline 会覆盖 rdi，caller 需在调用前后保存/恢复 slot base
    void pushRdi() {
        size_t at = here();
        emitByte(0x57);
        asmLineAt(at, "push rdi");
    }
    void popRdi() {
        size_t at = here();
        emitByte(0x5f);
        asmLineAt(at, "pop rdi");
    }
    void pushRax() {
        size_t at = here();
        emitByte(0x50);
        asmLineAt(at, "push rax");
    }
    void popRax() {
        size_t at = here();
        emitByte(0x58);
        asmLineAt(at, "pop rax");
    }
    void pushRbx() {
        size_t at = here();
        emitByte(0x53);
        asmLineAt(at, "push rbx");
    }
    void popRbx() {
        size_t at = here();
        emitByte(0x5b);
        asmLineAt(at, "pop rbx");
    }
    void pushRsi() {
        size_t at = here();
        emitByte(0x56);
        asmLineAt(at, "push rsi");
    }
    void popRsi() {
        size_t at = here();
        emitByte(0x5e);
        asmLineAt(at, "pop rsi");
    }
    void subRsp8() {
        size_t at = here();
        emitBytes({0x48, 0x83, 0xec, 0x08});
        asmLineAt(at, "sub rsp, 8");
    }
    void addRsp8() {
        size_t at = here();
        emitBytes({0x48, 0x83, 0xc4, 0x08});
        asmLineAt(at, "add rsp, 8");
    }

    // Debug：栈 136 字节，保存区与 JitDebugContext 布局一致；JIT 调用 jitDebugTraceWrapper，
    // wrapper 将 ctx 拷入 thread_local 再调 stub，stub 只写 thread_local，不覆盖本栈，恢复正确。
    // rdi 为 slot 基址，trace 路径会破坏 rdi，必须在 call 前保存、call 后恢复。将 rdi 额外存到
    // [rsp+128]，恢复时从备份取，避免 callee 写 shadow/栈覆盖 [rsp+64] 导致 JOIN 等读到错误 frame。
    void emitDebugTraceCall(uint32_t pc, void *fnAddr) {
        constexpr uint8_t off_rax = 112, off_rcx = 104, off_rdx = 96;
        constexpr uint8_t off_r8 = 56, off_r9 = 48, off_r10 = 40, off_r11 = 32;
        constexpr uint8_t off_rdi     = 64;  // JitDebugContext.rdi
        constexpr uint8_t off_rdi_bak = 128; // 备份，恢复时用，避免 [rsp+64] 被 callee 覆盖
        size_t blockStart             = here();
        emitBytes({0x48, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00}); // sub rsp, 136
        emitBytes({0x48, 0x89, 0x44, 0x24, off_rax});
        emitBytes({0x48, 0x89, 0x4c, 0x24, off_rcx});
        emitBytes({0x48, 0x89, 0x54, 0x24, off_rdx});
        emitBytes({0x4c, 0x89, 0x44, 0x24, off_r8});
        emitBytes({0x4c, 0x89, 0x4c, 0x24, off_r9});
        emitBytes({0x4c, 0x89, 0x54, 0x24, off_r10});
        emitBytes({0x4c, 0x89, 0x5c, 0x24, off_r11});
        emitBytes({0x48, 0x89, 0xbc, 0x24, off_rdi, 0x00, 0x00, 0x00}); // mov [rsp+64], rdi
        emitBytes(
            {0x48, 0x89, 0xbc, 0x24, off_rdi_bak, 0x00, 0x00, 0x00}); // mov [rsp+128], rdi 备份
        emitBytes({0xc7, 0x84, 0x24, 0x78, 0x00, 0x00, 0x00});
        emitBytes({
            static_cast<uint8_t>(pc & 0xff),
            static_cast<uint8_t>((pc >> 8) & 0xff),
            static_cast<uint8_t>((pc >> 16) & 0xff),
            static_cast<uint8_t>((pc >> 24) & 0xff),
        });
        emitBytes(
            {0xc7, 0x84, 0x24, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}); // mov dword
                                                                                 // [rsp+124], 0
        emitBytes({0x48, 0x8d, 0x0c, 0x24});
        if (fnAddr) {
            uint64_t addr = reinterpret_cast<uint64_t>(fnAddr);
            emitByte(0x48);
            emitByte(0xb8);
            emitBytes({
                static_cast<uint8_t>(addr & 0xff),
                static_cast<uint8_t>((addr >> 8) & 0xff),
                static_cast<uint8_t>((addr >> 16) & 0xff),
                static_cast<uint8_t>((addr >> 24) & 0xff),
                static_cast<uint8_t>((addr >> 32) & 0xff),
                static_cast<uint8_t>((addr >> 40) & 0xff),
                static_cast<uint8_t>((addr >> 48) & 0xff),
                static_cast<uint8_t>((addr >> 56) & 0xff),
            });
            emitBytes({0xff, 0xd0});
        } else {
            for (int i = 0; i < 16; ++i)
                emitByte(0x90);
        }
        // 先恢复 rdi（slot 基址），再恢复其它寄存器，避免 JOIN 等后续指令用错 frame
        emitBytes({0x48, 0x8b, 0xbc, 0x24, off_rdi_bak, 0x00, 0x00, 0x00}); // mov rdi, [rsp+128]
        emitBytes({0x48, 0x8b, 0x44, 0x24, off_rax});
        emitBytes({0x48, 0x8b, 0x4c, 0x24, off_rcx});
        emitBytes({0x48, 0x8b, 0x54, 0x24, off_rdx});
        emitBytes({0x4c, 0x8b, 0x44, 0x24, off_r8});
        emitBytes({0x4c, 0x8b, 0x4c, 0x24, off_r9});
        emitBytes({0x4c, 0x8b, 0x54, 0x24, off_r10});
        emitBytes({0x4c, 0x8b, 0x5c, 0x24, off_r11});
        emitBytes({0x48, 0x81, 0xc4, 0x88, 0x00, 0x00, 0x00}); // add rsp, 136
        asmLineAt(blockStart, "debug_trace pc=" + std::to_string(pc));
    }

    // call rel32
    void callRel32(int32_t rel) {
        emitByte(0xe8);
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLine("call .+" + std::to_string(rel) + "  ; rel32");
    }

    // nop (padding)
    void nop() {
        emitByte(0x90);
        asmLine("nop");
    }

    // cmp rax, imm8; setle al; movzx rax, al (result 0 or 1)
    void cmpRaxImm8Setle() {
        emitBytes({0x48, 0x83, 0xf8, 0x01}); // cmp rax, 1
        asmLine("cmp rax, 1");
        emitBytes({0x0f, 0x9e, 0xc0}); // setle al
        asmLine("setle al");
        emitBytes({0x48, 0x0f, 0xb6, 0xc0}); // movzx rax, al
        asmLine("movzx rax, al");
    }

    // test rax, rax（设置 ZF，供后续 cmove 使用）
    void testRaxRax() {
        size_t at = here();
        emitBytes({0x48, 0x85, 0xc0});
        asmLineAt(at, "test rax, rax");
    }
    // test reg, reg（不破坏 rax，用于 JOIN 的 vtest 后接 vcmovnz）
    void testRegReg(uint8_t reg) {
        if (reg == 0) {
            testRaxRax();
            return;
        }
        size_t at      = here();
        uint8_t regEnc = (reg <= 3) ? reg : (reg >= 4 && reg <= 7) ? (reg - 4) : (reg == 8) ? 7 : 6;
        uint8_t rex    = 0x48;
        if (reg >= 4 && reg <= 7)
            rex |= 4;
        emitByte(rex);
        emitByte(0x85);
        emitByte(0xC0 | regEnc | (regEnc << 3));
        asmLineAt(at, std::string("test ") + regName(reg) + ", " + regName(reg));
    }

    // cmove rcx, rbx（ZF=1 时 rcx=rbx），用于 JOIN 根据分支索引选值
    void cmoveRcxFromRbx() {
        emitBytes({0x48, 0x0f, 0x44, 0xcb});
        asmLine("cmove rcx, rbx");
    }

    // cmove r8, r9（ZF=1 时 r8=r9），用于 BRCH 在 test rax,rax 后选 0/1
    void cmoveR8FromR9() {
        emitBytes({0x4d, 0x0f, 0x44, 0xc1}); // REX.WRB 0F 44 /r: reg=r8, r/m=r9
        asmLine("cmove r8, r9");
    }

    // cmove/cmovnz 通用两寄存器（用于 VReg 分配后的 JOIN 等）
    void cmoveRegFromReg(uint8_t dst, uint8_t src) {
        size_t at   = here();
        uint8_t rex = 0x48;
        if (dst >= 4 && dst <= 7)
            rex |= 1;
        if (src >= 4 && src <= 7)
            rex |= 4;
        // ModRM: reg=dst, r/m=src → cmove dst, src（ZF=1 时 dst:=src）
        emitByte(rex);
        emitBytes({0x0f, 0x44, static_cast<uint8_t>(0xC0 | ((dst & 7) << 3) | (src & 7))});
        asmLineAt(at, std::string("cmove ") + regName(dst) + ", " + regName(src));
    }
    void cmovnzRegFromReg(uint8_t dst, uint8_t src) {
        size_t at   = here();
        uint8_t rex = 0x48;
        if (dst >= 4 && dst <= 7)
            rex |= 1;
        if (src >= 4 && src <= 7)
            rex |= 4;
        emitByte(rex);
        // ModRM: reg=dst(目标), r/m=src(源) → cmovnz dst, src（ZF=0 时 dst:=src）
        emitBytes({0x0f, 0x45, static_cast<uint8_t>(0xC0 | ((dst & 7) << 3) | (src & 7))});
        asmLineAt(at, std::string("cmovnz ") + regName(dst) + ", " + regName(src));
    }

    // test rax, rax; jz rel32 (jump if cond is false)
    void testRaxJzRel32(int32_t rel) {
        emitBytes({0x48, 0x85, 0xc0}); // test rax, rax
        asmLine("test rax, rax");
        jzRel32(rel);
    }

    // 仅 jz rel32（ZF 已由前一条 test/cmov 等设置）
    void jzRel32(int32_t rel) {
        size_t at = here();
        emitBytes({0x0f, 0x84}); // jz rel32
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLineAt(at, "jz .+" + std::to_string(rel) + "  ; rel32");
    }

    // mov rax, imm64
    void movRaxImm64(uint64_t imm) {
        size_t at = here();
        emitByte(0x48);
        emitByte(0xb8);
        emitBytes({
            static_cast<uint8_t>(imm & 0xff),
            static_cast<uint8_t>((imm >> 8) & 0xff),
            static_cast<uint8_t>((imm >> 16) & 0xff),
            static_cast<uint8_t>((imm >> 24) & 0xff),
            static_cast<uint8_t>((imm >> 32) & 0xff),
            static_cast<uint8_t>((imm >> 40) & 0xff),
            static_cast<uint8_t>((imm >> 48) & 0xff),
            static_cast<uint8_t>((imm >> 56) & 0xff),
        });
        std::ostringstream hex;
        hex << "0x" << std::hex << imm;
        asmLineAt(at, "mov rax, " + hex.str());
    }

    // call [rax] - call through rax (used after movRaxImm64)
    // Win64: call 前 RSP 必须 16 字节对齐。CallRax 前必有 PushRdi，push 后 RSP=0(mod 16)，
    // 故仅需 sub 32（shadow），无需额外 8 字节对齐；sub 40 会导致 call 前 RSP=8(mod 16)，违反 ABI。
    void callRax() {
        size_t at;
#if defined(_WIN32) || defined(_WIN64)
        at = here();
        emitBytes({0x48, 0x83, 0xec, 0x20}); // sub rsp, 32
        asmLineAt(at, "sub rsp, 32  ; shadow");
#endif
        at = here();
        emitBytes({0xff, 0xd0}); // call rax
        asmLineAt(at, "call rax");
#if defined(_WIN32) || defined(_WIN64)
        at = here();
        emitBytes({0x48, 0x83, 0xc4, 0x20}); // add rsp, 32
        asmLineAt(at, "add rsp, 32  ; shadow");
#endif
    }

    // 通用 mov r64, r64（用于 MIR 编码；reg 0-7=rax..r11，8=rdi，9=rsi）
    void emitMovRegReg(uint8_t dst, uint8_t src) {
        if (dst == src)
            return;
        size_t at   = here();
        auto encReg = [](uint8_t r, uint8_t &rexExtra, uint8_t &modrmBits) {
            if (r <= 3) {
                rexExtra  = 0;
                modrmBits = r;
                return;
            }
            if (r == 8) {
                rexExtra  = 0;
                modrmBits = 7;
                return; // rdi
            }
            if (r == 9) {
                rexExtra  = 0;
                modrmBits = 6;
                return; // rsi
            }
            rexExtra  = (r >= 4 && r <= 7) ? 1 : 0;
            modrmBits = (r >= 4 && r <= 7) ? (r - 4) : 0;
        };
        uint8_t rexB = 0, rexR = 0, rm = 0, reg = 0;
        uint8_t e1, e2;
        encReg(dst, e1, rm);
        if (dst >= 4 && dst <= 7)
            rexB = 1;
        encReg(src, e2, reg);
        if (src >= 4 && src <= 7)
            rexR = 4;
        emitByte(0x48 | rexB | rexR);
        emitByte(0x89);
        emitByte(0xC0 | rm | (reg << 3));
        asmLineAt(at, std::string("mov ") + regName(dst) + ", " + regName(src));
    }
    void emitMovRegImm32(uint8_t reg, uint32_t imm32) {
        size_t at = here();
        if (reg == 4) { // r8d
            emitByte(0x41);
            emitByte(0xb8);
        } else if (reg == 1) { // rcx
            emitByte(0x48);
            emitByte(0xc7);
            emitByte(0xc1);
        } else if (reg == 2) { // rdx
            emitByte(0x48);
            emitByte(0xc7);
            emitByte(0xc2);
        } else {
            emitByte(0x48);
            emitByte(0xc7);
            emitByte(0xc0 | (reg & 7));
        }
        emitBytes({
            static_cast<uint8_t>(imm32 & 0xff),
            static_cast<uint8_t>((imm32 >> 8) & 0xff),
            static_cast<uint8_t>((imm32 >> 16) & 0xff),
            static_cast<uint8_t>((imm32 >> 24) & 0xff),
        });
        asmLineAt(at, std::string("mov ") + regName(reg) + ", " + std::to_string(imm32));
    }
    void emitMovRegImm64(uint8_t reg, uint64_t imm64) {
        if (reg == 0) {
            movRaxImm64(imm64);
            return;
        }
        size_t at   = here();
        uint8_t rex = 0x48;
        uint8_t op  = 0xb8 | (reg & 7);
        if (reg >= 4 && reg <= 7) {
            rex = 0x49;
            op  = 0xb8 | (reg - 4);
        } else if (reg == 3) {
            rex = 0x48;
            op  = 0xbb; // rbx
        } else if (reg == 1) {
            op = 0xb9; // rcx
        } else if (reg == 2) {
            op = 0xba; // rdx
        } else if (reg == 8) {
            op = 0xbf; // rdi
        } else if (reg == 9) {
            op = 0xbe; // rsi
        }
        emitByte(rex);
        emitByte(op);
        emitBytes({
            static_cast<uint8_t>(imm64 & 0xff),
            static_cast<uint8_t>((imm64 >> 8) & 0xff),
            static_cast<uint8_t>((imm64 >> 16) & 0xff),
            static_cast<uint8_t>((imm64 >> 24) & 0xff),
            static_cast<uint8_t>((imm64 >> 32) & 0xff),
            static_cast<uint8_t>((imm64 >> 40) & 0xff),
            static_cast<uint8_t>((imm64 >> 48) & 0xff),
            static_cast<uint8_t>((imm64 >> 56) & 0xff),
        });
        std::ostringstream h;
        h << "mov " << regName(reg) << ", 0x" << std::hex << imm64;
        asmLineAt(at, h.str());
    }

    // Windows x64: preserve non-volatile regs, then copy rcx->rdi / rdx->rsi for internal use.
    // 额外 sub rsp, 8 让函数体内维持 RSP=8(mod16)，与现有 call/debug-trace 对齐假设保持一致。
    void prologueWin64() {
        size_t at1 = here();
        emitByte(0x57); // push rdi
        asmLineAt(at1, "push rdi");
        size_t at2 = here();
        emitByte(0x56); // push rsi
        asmLineAt(at2, "push rsi");
        size_t at3 = here();
        emitByte(0x53); // push rbx
        asmLineAt(at3, "push rbx");
        size_t at4 = here();
        emitBytes({0x48, 0x83, 0xec, 0x08}); // sub rsp, 8
        asmLineAt(at4, "sub rsp, 8  ; win64 align pad");
        size_t at5 = here();
        emitBytes({0x48, 0x89, 0xcf}); // mov rdi, rcx
        asmLineAt(at5, "mov rdi, rcx");
        size_t at6 = here();
        emitBytes({0x48, 0x89, 0xd6}); // mov rsi, rdx
        asmLineAt(at6, "mov rsi, rdx");
    }

    // Windows x64: call trampoline(frame, ctx, pc). Assumes rdi=frame, rsi=ctx.
    // trampoline/callee 会覆盖 rdi，返回后 JOIN 等需用 caller 的 frame，故调用前后 push/pop rdi。
    void callTrampolineWin64(uint32_t pc, uint64_t addr) {
        pushRdi();
        emitBytes({0x48, 0x89, 0xf9}); // mov rcx, rdi (arg1)
        asmLine("mov rcx, rdi");
        emitBytes({0x48, 0x89, 0xf2}); // mov rdx, rsi (arg2)
        asmLine("mov rdx, rsi");
        emitByte(0x41);
        emitByte(0xb8);
        emitBytes({
            static_cast<uint8_t>(pc & 0xff),
            static_cast<uint8_t>((pc >> 8) & 0xff),
            static_cast<uint8_t>((pc >> 16) & 0xff),
            static_cast<uint8_t>((pc >> 24) & 0xff),
        }); // mov r8d, imm32 (arg3)
        asmLine("mov r8d, " + std::to_string(pc));
        movRaxImm64(addr);
        callRax();
        popRdi();
    }

    // Windows x64: call trampolineOper(slots, ctx, pc). rdi=slots, rsi=ctx. graph 从 slots[0] 即
    // Frame* 获取。trampoline 会覆盖 rdi，返回后需恢复。
    void callTrampolineOperWin64(uint32_t pc, uint64_t addr) {
        pushRdi();
        emitBytes({0x48, 0x89, 0xf9}); // mov rcx, rdi (arg1 slots)
        asmLine("mov rcx, rdi");
        emitBytes({0x48, 0x89, 0xf2}); // mov rdx, rsi (arg2 ctx)
        asmLine("mov rdx, rsi");
        emitByte(0x41);
        emitByte(0xb8);
        emitBytes({
            static_cast<uint8_t>(pc & 0xff),
            static_cast<uint8_t>((pc >> 8) & 0xff),
            static_cast<uint8_t>((pc >> 16) & 0xff),
            static_cast<uint8_t>((pc >> 24) & 0xff),
        }); // mov r8d, imm32 (arg3 pc)
        asmLine("mov r8d, " + std::to_string(pc));
        movRaxImm64(addr);
        callRax();
        popRdi();
    }

    // SysV x64: call trampolineOper(slots, ctx, pc). rdi, rsi already set.
    void callTrampolineOperSysV(uint32_t pc, uint64_t addr) {
        emitBytes({0x48, 0xc7, 0xc2}); // mov rdx, imm32 (arg3 pc)
        emitBytes({
            static_cast<uint8_t>(pc & 0xff),
            static_cast<uint8_t>((pc >> 8) & 0xff),
            static_cast<uint8_t>((pc >> 16) & 0xff),
            static_cast<uint8_t>((pc >> 24) & 0xff),
        });
        asmLine("mov rdx, " + std::to_string(pc));
        movRaxImm64(addr);
        callRax();
    }

    // SysV x64: call trampoline(frame, ctx, pc). rdi, rsi already set; set rdx=pc.
    void callTrampolineSysV(uint32_t pc, uint64_t addr) {
        emitBytes({0x48, 0xc7, 0xc2}); // mov rdx, imm32
        emitBytes({
            static_cast<uint8_t>(pc & 0xff),
            static_cast<uint8_t>((pc >> 8) & 0xff),
            static_cast<uint8_t>((pc >> 16) & 0xff),
            static_cast<uint8_t>((pc >> 24) & 0xff),
        });
        asmLine("mov rdx, " + std::to_string(pc));
        movRaxImm64(addr);
        callRax();
    }

    // shl rax, 16  — clear top 16 bits (part 1 of 48-bit mask)
    void shlRax16() {
        size_t at = here();
        emitBytes({0x48, 0xC1, 0xE0, 0x10});
        asmLineAt(at, "shl rax, 16");
    }
    // shr rax, 16  — restore (part 2 of 48-bit mask)
    void shrRax16() {
        size_t at = here();
        emitBytes({0x48, 0xC1, 0xE8, 0x10});
        asmLineAt(at, "shr rax, 16");
    }
    // cmp rax, imm32 (sign-extended)
    void cmpRaxImm32(int32_t imm) {
        size_t at = here();
        emitBytes({0x48, 0x3D});
        emitBytes({
            static_cast<uint8_t>(imm & 0xff),
            static_cast<uint8_t>((imm >> 8) & 0xff),
            static_cast<uint8_t>((imm >> 16) & 0xff),
            static_cast<uint8_t>((imm >> 24) & 0xff),
        });
        asmLineAt(at, "cmp rax, " + std::to_string(imm));
    }
    // jb rel32 (0F 82 rel32) — returns offset of the rel32 field for patching
    size_t jbRel32(int32_t rel) {
        size_t at     = here();
        size_t relPos = at + 2;
        emitBytes({0x0F, 0x82});
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLineAt(at, "jb .+" + std::to_string(rel) + "  ; rel32");
        return relPos;
    }

    // --- Native JIT call helpers ---

    void movRcxRdi() {
        size_t at = here();
        emitBytes({0x48, 0x89, 0xf9});
        asmLineAt(at, "mov rcx, rdi");
    }
    void movRdxRsi() {
        size_t at = here();
        emitBytes({0x48, 0x89, 0xf2});
        asmLineAt(at, "mov rdx, rsi");
    }
    void movRbxImm64(uint64_t imm) { emitMovRegImm64(3, imm); }
    void movR8Imm64(uint64_t imm) { emitMovRegImm64(4, imm); }
    void subRspShadow() {
        size_t at = here();
        emitBytes({0x48, 0x83, 0xec, 0x20});
        asmLineAt(at, "sub rsp, 32  ; shadow");
    }
    void addRspShadow() {
        size_t at = here();
        emitBytes({0x48, 0x83, 0xc4, 0x20});
        asmLineAt(at, "add rsp, 32  ; shadow");
    }

    // --- Native self-call support (inline frame management) ---

    // mov r10, [rbx] — load from memory at rbx
    void movR10FromRbx() {
        size_t at = here();
        emitBytes({0x4C, 0x8B, 0x13});
        asmLineAt(at, "mov r10, [rbx]");
    }
    // mov r11, [r10 + disp8]
    void movR11FromR10Disp8(int8_t d) {
        size_t at = here();
        emitBytes({0x4D, 0x8B, 0x5A, static_cast<uint8_t>(d)});
        asmLineAt(at, "mov r11, [r10+" + std::to_string(d) + "]");
    }
    // mov [rbx], r11 — store r11 to [rbx]
    void movRbxFromR11() {
        size_t at = here();
        emitBytes({0x4C, 0x89, 0x1B});
        asmLineAt(at, "mov [rbx], r11");
    }
    // mov [rbx], r10 — store r10 to [rbx]
    void movRbxFromR10() {
        size_t at = here();
        emitBytes({0x4C, 0x89, 0x13});
        asmLineAt(at, "mov [rbx], r10");
    }
    // mov [r10 + disp8], r11
    void movR10Disp8FromR11(int8_t d) {
        size_t at = here();
        emitBytes({0x4D, 0x89, 0x5A, static_cast<uint8_t>(d)});
        asmLineAt(at, "mov [r10+" + std::to_string(d) + "], r11");
    }
    // mov qword [r11 + disp8], 0
    void movR11Disp8Imm0(int8_t d) {
        size_t at = here();
        emitBytes({0x49, 0xC7, 0x43, static_cast<uint8_t>(d), 0, 0, 0, 0});
        asmLineAt(at, "mov qword [r11+" + std::to_string(d) + "], 0");
    }
    // lea r11, [r10 + disp8]
    void leaR11R10Disp8(int8_t d) {
        size_t at = here();
        emitBytes({0x4D, 0x8D, 0x5A, static_cast<uint8_t>(d)});
        asmLineAt(at, "lea r11, [r10+" + std::to_string(d) + "]");
    }
    // mov [r11], r10
    void movR11FromR10Store() {
        size_t at = here();
        emitBytes({0x4D, 0x89, 0x13});
        asmLineAt(at, "mov [r11], r10");
    }
    // mov rax, [r11 + disp8]
    void movRaxFromR11Disp8(int8_t d) {
        size_t at = here();
        emitBytes({0x49, 0x8B, 0x43, static_cast<uint8_t>(d)});
        asmLineAt(at, "mov rax, [r11+" + std::to_string(d) + "]");
    }
    // mov [r11 + disp8], rax
    void movR11Disp8FromRax(int8_t d) {
        size_t at = here();
        emitBytes({0x49, 0x89, 0x43, static_cast<uint8_t>(d)});
        asmLineAt(at, "mov [r11+" + std::to_string(d) + "], rax");
    }
    // mov rdi, r11
    void movRdiR11() {
        size_t at = here();
        emitBytes({0x4C, 0x89, 0xDF});
        asmLineAt(at, "mov rdi, r11");
    }
    // cmp r11, rax
    void cmpR11Rax() {
        size_t at = here();
        emitBytes({0x4C, 0x3B, 0xD8});
        asmLineAt(at, "cmp r11, rax");
    }
    // jne rel32 (0F 85 rel32) — returns offset of the rel32 field for patching
    size_t jneRel32(int32_t rel) {
        size_t at     = here();
        size_t relPos = at + 2;
        emitBytes({0x0F, 0x85});
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLineAt(at, "jne .+" + std::to_string(rel) + "  ; rel32");
        return relPos;
    }
    // mov r10, [rdi] — load Frame* from callee's slot[0]
    void movR10FromRdi() {
        size_t at = here();
        emitBytes({0x4C, 0x8B, 0x17});
        asmLineAt(at, "mov r10, [rdi]");
    }
    // mov r11, [rbx]
    void movR11FromRbx() {
        size_t at = here();
        emitBytes({0x4C, 0x8B, 0x1B});
        asmLineAt(at, "mov r11, [rbx]");
    }
    // patchRel32At — write a rel32 at a given offset
    void patchRel32At(size_t pos, int32_t rel) {
        out_[pos + 0] = static_cast<uint8_t>(rel & 0xff);
        out_[pos + 1] = static_cast<uint8_t>((rel >> 8) & 0xff);
        out_[pos + 2] = static_cast<uint8_t>((rel >> 16) & 0xff);
        out_[pos + 3] = static_cast<uint8_t>((rel >> 24) & 0xff);
    }

  private:
    std::vector<uint8_t> &out_;
    std::ostream *asmOut_ = nullptr;
    size_t baseAddr_      = 0;
    bool recordAsm_       = false;
    std::vector<std::pair<size_t, std::string>> asmLines_;
};

} // namespace camel::jit::x64
