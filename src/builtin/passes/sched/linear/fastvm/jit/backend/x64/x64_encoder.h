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
 * Updated: Feb. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace camel::jit::x64 {

class Encoder {
  public:
    explicit Encoder(std::vector<uint8_t> &out, std::ostream *asmOut = nullptr, size_t baseAddr = 0)
        : out_(out), asmOut_(asmOut), baseAddr_(baseAddr) {}

    size_t size() const { return out_.size(); }
    size_t here() const { return out_.size(); }

    void emitByte(uint8_t b) { out_.push_back(b); }
    void asmLine(const std::string &s) {
        if (asmOut_) {
            std::ostringstream hex;
            hex << std::hex << (baseAddr_ + here());
            *asmOut_ << "  [" << hex.str() << "]  " << s << "\n";
        }
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
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x8b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x8b, 0x87});
            emitDisp32(disp);
        }
        asmLine("mov rax, [rdi+" + std::to_string(disp) + "]");
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
        asmLine(std::string("mov ") + regName(reg) + ", [rdi+" + std::to_string(disp) + "]");
    }

    // mov [rdi + disp], r64 (reg: 0=rax..7=r11)
    void movToFrame(int disp, uint8_t reg) {
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
        asmLine(std::string("mov [rdi+") + std::to_string(disp) + "], " + regName(reg));
    }

    // mov [rdi + disp], rax，64 位写回一槽
    void movFrameFromRax(int disp) {
        rexW();
        if (fitsDisp8(disp)) {
            emitBytes({0x89, 0x47, static_cast<uint8_t>(disp & 0xff)});
        } else {
            emitBytes({0x89, 0x87});
            emitDisp32(disp);
        }
        asmLine("mov [rdi+" + std::to_string(disp) + "], rax");
    }

    // mov reg, rax (reg: 0=rax no-op, 1..7=rcx..r11). 89 /r: r/m=dest, reg=rax
    void movRegFromRax(uint8_t reg) {
        if (reg == 0)
            return;
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWB(); // REX.B for R/M field (dest)
            modrm = static_cast<uint8_t>(0xC0 | (regEnc - 4));
        }
        emitBytes({0x89, modrm});
        asmLine(std::string("mov ") + regName(reg) + ", rax");
    }

    // mov rax, reg (reg: 1..7=rcx..r11). 89 /r: r/m=rax, reg=src
    void movRaxFromReg(uint8_t reg) {
        if (reg == 0)
            return;
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR(); // REX.R for Reg field (src)
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x89, modrm});
        asmLine(std::string("mov rax, ") + regName(reg));
    }

    // add rax, reg. 01 /r: r/m=rax, reg=src
    void addRaxFromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x01, modrm});
        asmLine(std::string("add rax, ") + regName(reg));
    }

    // sub rax, reg. 29 /r: r/m=rax, reg=src
    void subRaxFromReg(uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | ((regEnc & 3) << 3));
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0xC0 | ((regEnc - 4) << 3));
        }
        emitBytes({0x29, modrm});
        asmLine(std::string("sub rax, ") + regName(reg));
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
        asmLine(h.str());
        rexW();
        emitBytes({0x8b, 0x03}); // mov rax, [rbx]
        asmLine("mov rax, [rbx]");
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
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0xC0 | (regEnc & 7));
        if (regEnc < 4) {
            emitBytes({0x66, 0x48, 0x0f, 0x7e, modrm});
        } else {
            emitBytes({0x66, 0x49, 0x0f, 0x7e, modrm});
        }
        asmLine(std::string("movq ") + regName(reg) + ", xmm0");
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
        emitByte(0xe9);
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLine("jmp .+" + std::to_string(rel) + "  ; rel32");
    }

    // jle rel32
    void jleRel32(int32_t rel) {
        emitBytes({0x0f, 0x8e});
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLine("jle .+" + std::to_string(rel) + "  ; rel32");
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
        emitByte(0xc3);
        asmLine("ret");
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
        emitBytes({0x48, 0x85, 0xc0});
        asmLine("test rax, rax");
    }

    // cmove rcx, rbx（ZF=1 时 rcx=rbx），用于 JOIN 根据分支索引选值
    void cmoveRcxFromRbx() {
        emitBytes({0x48, 0x0f, 0x44, 0xcb});
        asmLine("cmove rcx, rbx");
    }

    // test rax, rax; jz rel32 (jump if cond is false)
    void testRaxJzRel32(int32_t rel) {
        emitBytes({0x48, 0x85, 0xc0}); // test rax, rax
        asmLine("test rax, rax");
        emitBytes({0x0f, 0x84}); // jz rel32
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
        asmLine("jz .+" + std::to_string(rel) + "  ; rel32");
    }

    // mov rax, imm64
    void movRaxImm64(uint64_t imm) {
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
        asmLine("mov rax, " + hex.str());
    }

    // call [rax] - call through rax (used after movRaxImm64)
    void callRax() {
        emitBytes({0xff, 0xd0}); // call rax
        asmLine("call rax");
    }

    // 通用 mov r64, r64（用于 MIR 编码；reg 0-7=rax..r11，8=rdi，9=rsi）
    void emitMovRegReg(uint8_t dst, uint8_t src) {
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
        asmLine(std::string("mov ") + regName(dst) + ", " + regName(src));
    }
    void emitMovRegImm32(uint8_t reg, uint32_t imm32) {
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
        asmLine(std::string("mov ") + regName(reg) + ", " + std::to_string(imm32));
    }
    void emitMovRegImm64(uint8_t reg, uint64_t imm64) {
        if (reg == 0) {
            movRaxImm64(imm64);
            return;
        }
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
        asmLine(h.str());
    }

    // Windows x64: copy rcx->rdi, rdx->rsi (SysV convention for internal use)
    void prologueWin64() {
        emitBytes({0x48, 0x89, 0xcf}); // mov rdi, rcx
        asmLine("mov rdi, rcx");
        emitBytes({0x48, 0x89, 0xd6}); // mov rsi, rdx
        asmLine("mov rsi, rdx");
    }

    // Windows x64: call trampoline(frame, ctx, pc). Assumes rdi=frame, rsi=ctx.
    void callTrampolineWin64(uint32_t pc, uint64_t addr) {
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
    }

    // Windows x64: call trampolineOper(slots, ctx, pc, graph). rdi=slots, rsi=ctx.
    void callTrampolineOperWin64(uint32_t pc, uint64_t graphPtr, uint64_t addr) {
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
        emitByte(0x49);
        emitByte(0xb9); // mov r9, imm64 (arg4 graph)
        emitBytes({
            static_cast<uint8_t>(graphPtr & 0xff),
            static_cast<uint8_t>((graphPtr >> 8) & 0xff),
            static_cast<uint8_t>((graphPtr >> 16) & 0xff),
            static_cast<uint8_t>((graphPtr >> 24) & 0xff),
            static_cast<uint8_t>((graphPtr >> 32) & 0xff),
            static_cast<uint8_t>((graphPtr >> 40) & 0xff),
            static_cast<uint8_t>((graphPtr >> 48) & 0xff),
            static_cast<uint8_t>((graphPtr >> 56) & 0xff),
        });
        asmLine("mov r9, graph");
        movRaxImm64(addr);
        callRax();
    }

    // SysV x64: call trampolineOper(slots, ctx, pc, graph). rdi, rsi already set.
    void callTrampolineOperSysV(uint32_t pc, uint64_t graphPtr, uint64_t addr) {
        emitBytes({0x48, 0xc7, 0xc2}); // mov rdx, imm32 (arg3 pc)
        emitBytes({
            static_cast<uint8_t>(pc & 0xff),
            static_cast<uint8_t>((pc >> 8) & 0xff),
            static_cast<uint8_t>((pc >> 16) & 0xff),
            static_cast<uint8_t>((pc >> 24) & 0xff),
        });
        asmLine("mov rdx, " + std::to_string(pc));
        emitByte(0x48);
        emitByte(0xb9); // mov rcx, imm64 (arg4 graph) - SysV 4th arg is rcx
        emitBytes({
            static_cast<uint8_t>(graphPtr & 0xff),
            static_cast<uint8_t>((graphPtr >> 8) & 0xff),
            static_cast<uint8_t>((graphPtr >> 16) & 0xff),
            static_cast<uint8_t>((graphPtr >> 24) & 0xff),
            static_cast<uint8_t>((graphPtr >> 32) & 0xff),
            static_cast<uint8_t>((graphPtr >> 40) & 0xff),
            static_cast<uint8_t>((graphPtr >> 48) & 0xff),
            static_cast<uint8_t>((graphPtr >> 56) & 0xff),
        });
        asmLine("mov rcx, graph");
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

  private:
    std::vector<uint8_t> &out_;
    std::ostream *asmOut_ = nullptr;
    size_t baseAddr_      = 0;
};

} // namespace camel::jit::x64
