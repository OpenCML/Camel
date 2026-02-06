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
 * Updated: Feb. 07, 2026
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

    // REX.W prefix (48), REX.WR (4C) for Reg field extended, REX.WB (49) for R/M field extended
    void rexW() { emitByte(0x48); }
    void rexWR() { emitByte(0x4C); }
    void rexWB() { emitByte(0x49); }

    // mov rax, [rdi + disp8]
    void movRaxFromFrame(int disp) {
        rexW();
        emitBytes({0x8b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        asmLine("mov rax, [rdi+" + std::to_string(disp) + "]");
    }

    static const char *regName(uint8_t r) {
        static const char *n[] = {"rax", "rcx", "rdx", "rbx", "r8", "r9", "r10", "r11"};
        return n[r & 7];
    }

    // mov reg, [rdi + disp8] (reg: 0=rax..3=rbx, 4=r8..7=r11)
    void movRegFromFrame(uint8_t reg, int disp) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0x40 | ((regEnc & 3) << 3) | 7);
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR(); // REX.R for Reg field
            modrm = static_cast<uint8_t>(0x40 | ((regEnc - 4) << 3) | 7);
        }
        emitBytes({0x8b, modrm, static_cast<uint8_t>(disp & 0xff)});
        asmLine(std::string("mov ") + regName(reg) + ", [rdi+" + std::to_string(disp) + "]");
    }

    // mov [rdi + disp8], r64 (reg: 0=rax..7=r11)
    void movToFrame(int disp, uint8_t reg) {
        uint8_t regEnc = reg & 7;
        uint8_t modrm  = static_cast<uint8_t>(0x40 | ((regEnc & 3) << 3) | 7);
        if (regEnc < 4) {
            rexW();
        } else {
            rexWR();
            modrm = static_cast<uint8_t>(0x40 | ((regEnc - 4) << 3) | 7);
        }
        emitBytes({0x89, modrm, static_cast<uint8_t>(disp & 0xff)});
        asmLine(std::string("mov [rdi+") + std::to_string(disp) + "], " + regName(reg));
    }

    // mov [rdi + disp8], rax
    void movFrameFromRax(int disp) {
        rexW();
        emitBytes({0x89, 0x47, static_cast<uint8_t>(disp & 0xff)});
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

    // add rax, [rdi + disp8]
    void addRaxFromFrame(int disp) {
        rexW();
        emitBytes({0x03, 0x47, static_cast<uint8_t>(disp & 0xff)});
        asmLine("add rax, [rdi+" + std::to_string(disp) + "]");
    }

    // sub rax, [rdi + disp8]
    void subRaxFromFrame(int disp) {
        rexW();
        emitBytes({0x2b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        asmLine("sub rax, [rdi+" + std::to_string(disp) + "]");
    }

    // cmp [rdi + disp8], imm32 (signed)
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

    // cmp rax, [rdi + disp8]  then jcc
    void cmpRaxWithFrame(int disp) {
        rexW();
        emitBytes({0x3b, 0x47, static_cast<uint8_t>(disp & 0xff)});
        asmLine("cmp rax, [rdi+" + std::to_string(disp) + "]");
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
