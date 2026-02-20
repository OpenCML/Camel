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
 * Updated: Feb. 17, 2026
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

// 虚拟寄存器：在 MIR 中表示“未分配”的值，由统一寄存器分配阶段赋给物理寄存器
using VRegId                  = uint16_t;
constexpr VRegId kInvalidVReg = 0xFFFFu;
constexpr VRegId kMaxVRegs    = 512;

inline const char *mirRegName(uint8_t r) {
    static const char *n[] = {"rax", "rcx", "rdx", "rbx", "r8", "r9", "r10", "r11", "rdi", "rsi"};
    return (r <= 9u) ? n[r] : "?";
}

// L3 扩展：可在此增加 blockId、pred/succ 等
struct MirBlockInfo {
    uint32_t blockId{0}; // 0 = 未分配，L3 用于 CFG
};

// 仅保留 ABI/控制流；其余全部为 V*（虚拟寄存器），由 linearScanVReg 统一分配
enum class MirOp : uint8_t {
    MovRegReg,
    MovRegImm32,
    MovRegImm64,
    PushRdi, // 保存 rdi（slot base）跨越 trampoline 调用；trampoline 会覆盖 rdi
    PopRdi,  // 恢复 rdi
    CallRax,
    JzRel32,
    JmpRel32,
    JleRel32,
    JmpRel8,
    JleRel8,
    Ret, // 无操作数，用于 TAIL 等
    // --- V*：r0/r1=VRegId，三操作数时右操作数在 imm32（vreg id）---
    VLoadFromFrame,
    VStoreToFrame,
    VLoadFromMemAt,
    VLoadImm32,
    VLoadImm64,
    VCopy,
    VTest,
    VCmove,
    VCmovnz,
    VMovFromRax,
    VMovToRax,
    VRet,
    VAdd,
    VSub,
    VMul,
    VIdiv,
    VCmpSetL,
    VCmpSetLE,
    VCmpSetG,
    VCmpSetGE,
    VCmpSetE,
    VCmpSetNE,
    VXmmLoadFromFrame,
    VXmmStoreToFrame,
    VXmmLoadFromMemAt,
    VXmmAdd,
    VXmmSub,
    VXmmMul,
    VXmmDiv,
    VXmmCmpSetB,
    VXmmCmpSetBE,
    VXmmCmpSetE,
    VXmmCmpSetA,
    VXmmCmpSetAE,
    VXmmCmpSetNZ,
    VAdd32,
    VSub32,
    VMul32,
    VIdiv32,
    VCmpSetL32,
    VCmpSetLE32,
    VCmpSetG32,
    VCmpSetGE32,
    VCmpSetE32,
    VCmpSetNE32,
    VXmm32LoadFromFrame,
    VXmm32StoreToFrame,
    VXmm32LoadFromMemAt,
    VXmm32Add,
    VXmm32Sub,
    VXmm32Mul,
    VXmm32Div,
    VXmm32CmpSetB,
    VXmm32CmpSetBE,
    VXmm32CmpSetE,
    VXmm32CmpSetA,
    VXmm32CmpSetAE,
    VXmm32CmpSetNZ,
    DebugTrace, // 调试用：调用 jitDebugTrace(ctx)，打印 GPR + pc；仅 Debug 构建插入
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
// vregAlloc 传 camel::jit::VRegAllocation*（调用方 include regalloc.h），此处用 void* 避免 mir.h
// 依赖 regalloc
struct MirPrintOptions {
    const std::unordered_map<size_t, size_t> *pcToOffset         = nullptr;
    const std::unordered_map<uint64_t, std::string> *symbolNames = nullptr; // 地址 -> 函数/符号名
    const std::unordered_map<int, std::string> *slotNames = nullptr; // disp(rdi+) -> 槽位名如 "n"
    const void *vregAlloc                                 = nullptr;
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
