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

/**
 * Machine IR (MIR) 层：与 backend 并列的中端结构。当前实现为 x64 的机器级 IR（线性指令序列），
 * 便于优化与可读打印，并为 L3（基本块/def-use）预留扩展。后续可在此层引入与架构无关的抽象 MIR，
 * 或为其它 target 增加对应 MIR 变体。
 */

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::jit::x64 {

// 无效 pc 标记（未绑定到某条字节码）
constexpr uint32_t kMirInvalidPc = 0xFFFFFFFFu;

// 寄存器编号：0-7 = rax,rcx,rdx,rbx,r8,r9,r10,r11；8=rdi，9=rsi（ABI 用）
constexpr uint8_t kRegRax = 0, kRegRcx = 1, kRegRdx = 2, kRegRbx = 3;
constexpr uint8_t kRegR8 = 4, kRegR9 = 5, kRegR10 = 6, kRegR11 = 7;
constexpr uint8_t kRegRdi = 8, kRegRsi = 9;

inline const char *mirRegName(uint8_t r) {
    static const char *n[] = {"rax", "rcx", "rdx", "rbx", "r8", "r9", "r10", "r11", "rdi", "rsi"};
    return (r <= 9u) ? n[r] : "?";
}

// L3 扩展：可在此增加 blockId、pred/succ 等
struct MirBlockInfo {
    uint32_t blockId{0}; // 0 = 未分配，L3 用于 CFG
};

enum class MirOp : uint8_t {
    // 寄存器-寄存器
    MovRegReg,
    // 立即数
    MovRegImm32,
    MovRegImm64,
    // Frame [rdi+disp]
    MovRegFromFrame,
    MovFrameFromReg,
    // 内存（静态区等，用 rbx 作基址）
    MovRaxFromMemAt,
    AddRaxFromMemAt,
    SubRaxFromMemAt,
    // 算术 (rax 与 reg/frame/mem)
    MovRaxFromReg,
    MovRegFromRax,
    AddRaxFromReg,
    SubRaxFromReg,
    AddRaxFromFrame,
    SubRaxFromFrame,
    // XMM0 与 frame/reg/mem
    MovXmm0FromFrame,
    AddXmm0FromFrame,
    SubXmm0FromFrame,
    MovFrameFromXmm0,
    MovXmm0FromReg,
    AddXmm0FromReg,
    SubXmm0FromReg,
    MovRegFromXmm0,
    MovXmm0FromMemAt,
    AddXmm0FromMemAt,
    SubXmm0FromMemAt,
    // 比较与条件（Long 两 slot 比较，结果 0/1 入 rax）
    CmpRaxImm8Setle,
    CmpRaxFrameSetle,
    CmpRaxMemAtSetle,
    CmpRaxFrameSetl,
    CmpRaxMemAtSetl,
    CmpRaxFrameSetg,
    CmpRaxMemAtSetg,
    CmpRaxFrameSete,
    CmpRaxMemAtSete,
    CmpRaxFrameSetne,
    CmpRaxMemAtSetne,
    CmpRaxFrameSetge,
    CmpRaxMemAtSetge,
    // Long 算术
    MulRaxFromReg,
    MulRaxFromFrame,
    MulRaxFromMemAt,
    IdivRaxByReg,
    IdivRaxByFrame,
    IdivRaxByMemAt,
    // Double 算术
    MulXmm0FromReg,
    MulXmm0FromFrame,
    MulXmm0FromMemAt,
    DivXmm0FromReg,
    DivXmm0FromFrame,
    DivXmm0FromMemAt,
    // Double 比较：comisd xmm0, op; setcc al; movzx rax, al
    ComisdXmm0FrameSetb,
    ComisdXmm0MemAtSetb,
    ComisdXmm0RegSetb,
    ComisdXmm0FrameSetbe,
    ComisdXmm0MemAtSetbe,
    ComisdXmm0RegSetbe,
    ComisdXmm0FrameSete,
    ComisdXmm0MemAtSete,
    ComisdXmm0RegSete,
    ComisdXmm0FrameSeta,
    ComisdXmm0MemAtSeta,
    ComisdXmm0RegSeta,
    ComisdXmm0FrameSetae,
    ComisdXmm0MemAtSetae,
    ComisdXmm0RegSetae,
    ComisdXmm0FrameSetnz,
    ComisdXmm0MemAtSetnz,
    ComisdXmm0RegSetnz,
    // 32 位整型 (I*)
    MovEaxFromFrame,
    MovEaxFromReg,
    MovEaxFromMemAt,
    AddEaxFromFrame,
    AddEaxFromReg,
    AddEaxFromMemAt,
    SubEaxFromFrame,
    SubEaxFromReg,
    SubEaxFromMemAt,
    MulEaxFromFrame,
    MulEaxFromReg,
    MulEaxFromMemAt,
    IdivEaxByFrame,
    IdivEaxByReg,
    IdivEaxByMemAt,
    CmpEaxFrameSetl,
    CmpEaxMemAtSetl,
    CmpEaxRegSetl,
    CmpEaxFrameSetg,
    CmpEaxMemAtSetg,
    CmpEaxRegSetg,
    CmpEaxFrameSete,
    CmpEaxMemAtSete,
    CmpEaxRegSete,
    CmpEaxFrameSetne,
    CmpEaxMemAtSetne,
    CmpEaxRegSetne,
    CmpEaxFrameSetle,
    CmpEaxMemAtSetle,
    CmpEaxRegSetle,
    CmpEaxFrameSetge,
    CmpEaxMemAtSetge,
    CmpEaxRegSetge,
    // 32 位浮点 (F*)
    MovSsXmm0FromFrame,
    MovSsXmm0FromReg,
    MovSsXmm0FromMemAt,
    AddSsXmm0FromFrame,
    AddSsXmm0FromReg,
    AddSsXmm0FromMemAt,
    SubSsXmm0FromFrame,
    SubSsXmm0FromReg,
    SubSsXmm0FromMemAt,
    MulSsXmm0FromFrame,
    MulSsXmm0FromReg,
    MulSsXmm0FromMemAt,
    DivSsXmm0FromFrame,
    DivSsXmm0FromReg,
    DivSsXmm0FromMemAt,
    MovSsFrameFromXmm0,
    MovSsRegFromXmm0,
    ComissXmm0FrameSetb,
    ComissXmm0MemAtSetb,
    ComissXmm0RegSetb,
    ComissXmm0FrameSetbe,
    ComissXmm0MemAtSetbe,
    ComissXmm0RegSetbe,
    ComissXmm0FrameSete,
    ComissXmm0MemAtSete,
    ComissXmm0RegSete,
    ComissXmm0FrameSeta,
    ComissXmm0MemAtSeta,
    ComissXmm0RegSeta,
    ComissXmm0FrameSetae,
    ComissXmm0MemAtSetae,
    ComissXmm0RegSetae,
    ComissXmm0FrameSetnz,
    ComissXmm0MemAtSetnz,
    ComissXmm0RegSetnz,
    TestRaxRax,
    TestRaxJzRel32, // targetPc 有效（含 test rax,rax）
    JzRel32,        // targetPc 有效，仅 jz（ZF 已由前一条指令设置）
    CmoveRcxFromRbx,
    CmoveR8FromR9, // ZF=1 时 r8=r9，用于 BRCH 单次写 result
    // 控制流（targetPc 或 rel 在 imm32/disp 中）
    JmpRel32, // targetPc 有效
    JleRel32, // targetPc 有效
    JmpRel8,
    JleRel8,
    Ret,
    CallRax,
    // 占位/调试
    Nop,
};

struct Mir {
    MirOp op{MirOp::Nop};
    uint8_t r0{0};
    uint8_t r1{0};
    int32_t disp{0};
    uint32_t imm32{0};
    uint64_t imm64{0};
    uint32_t pc{kMirInvalidPc}; // 若为首条对应某字节码则设为该 pc，用于 pcToOffset
    MirBlockInfo block;         // L3 扩展

    bool hasPc() const { return pc != kMirInvalidPc; }
    void clearPc() { pc = kMirInvalidPc; }
};

using MirBuffer = std::vector<Mir>;

// 可读字符串，便于 debug 打印（如 "mov rdi, rcx"）
std::string mirToString(const Mir &m);

// MIR 打印选项：仅首条对应某 pc 的指令显示 pc，其余 pc 列留空；可选符号名与槽位名注释
struct MirPrintOptions {
    const std::unordered_map<size_t, size_t> *pcToOffset         = nullptr;
    const std::unordered_map<uint64_t, std::string> *symbolNames = nullptr; // 地址 -> 函数/符号名
    const std::unordered_map<int, std::string> *slotNames = nullptr; // disp(rdi+) -> 槽位名如 "n"
};

// 整段 buffer 打印到 stream，格式 "[pc][idx]  insn  ; 注释"。pc 列仅该 pc 的首条 MIR
// 有值，其余空白。
void mirPrint(const MirBuffer &buf, std::ostream &out, const MirPrintOptions &opts = {});

// 单条 MIR 编码后的字节数（用于优化后计算 pcToOffset）
size_t mirSizeBytes(const Mir &m);

// 整段 buffer 总字节数
inline size_t mirBufferSizeBytes(const MirBuffer &buf) {
    size_t n = 0;
    for (const auto &m : buf)
        n += mirSizeBytes(m);
    return n;
}

} // namespace camel::jit::x64
