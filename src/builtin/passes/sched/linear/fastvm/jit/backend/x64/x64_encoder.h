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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <vector>

namespace camel::jit::x64 {

class Encoder {
  public:
    explicit Encoder(std::vector<uint8_t> &out) : out_(out) {}

    size_t size() const { return out_.size(); }
    size_t here() const { return out_.size(); }

    void emitByte(uint8_t b) { out_.push_back(b); }
    void emitBytes(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes)
            out_.push_back(b);
    }

    // REX.W prefix (48)
    void rexW() { emitByte(0x48); }

    // mov rax, [rdi + disp8]
    void movRaxFromFrame(int disp) {
        rexW();
        emitBytes({0x8b, 0x47, static_cast<uint8_t>(disp & 0xff)});
    }

    // mov [rdi + disp8], r64 (reg: 0=rax, 1=rcx, ...)
    void movToFrame(int disp, uint8_t reg) {
        rexW();
        emitBytes(
            {0x89, static_cast<uint8_t>(0x40 | (reg << 3) | 7), static_cast<uint8_t>(disp & 0xff)});
    }

    // mov [rdi + disp8], rax
    void movFrameFromRax(int disp) {
        rexW();
        emitBytes({0x89, 0x47, static_cast<uint8_t>(disp & 0xff)});
    }

    // add rax, [rdi + disp8]
    void addRaxFromFrame(int disp) {
        rexW();
        emitBytes({0x03, 0x47, static_cast<uint8_t>(disp & 0xff)});
    }

    // sub rax, [rdi + disp8]
    void subRaxFromFrame(int disp) {
        rexW();
        emitBytes({0x2b, 0x47, static_cast<uint8_t>(disp & 0xff)});
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
    }

    // cmp rax, [rdi + disp8]  then jcc
    void cmpRaxWithFrame(int disp) {
        rexW();
        emitBytes({0x3b, 0x47, static_cast<uint8_t>(disp & 0xff)});
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
    }

    // jmp rel8 (短跳转, -128..127)
    void jmpRel8(int8_t rel) {
        emitByte(0xeb);
        emitByte(static_cast<uint8_t>(rel & 0xff));
    }

    // jle rel8
    void jleRel8(int8_t rel) { emitBytes({0x7e, static_cast<uint8_t>(rel & 0xff)}); }

    // ret
    void ret() { emitByte(0xc3); }

    // call rel32
    void callRel32(int32_t rel) {
        emitByte(0xe8);
        emitBytes({
            static_cast<uint8_t>(rel & 0xff),
            static_cast<uint8_t>((rel >> 8) & 0xff),
            static_cast<uint8_t>((rel >> 16) & 0xff),
            static_cast<uint8_t>((rel >> 24) & 0xff),
        });
    }

    // nop (padding)
    void nop() { emitByte(0x90); }

  private:
    std::vector<uint8_t> &out_;
};

} // namespace camel::jit::x64
