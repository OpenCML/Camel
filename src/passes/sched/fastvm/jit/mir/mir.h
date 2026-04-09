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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * Machine IR (MIR) layer: the mid-end structure parallel to backend. The current implementation is
 * a x64 machine-level IR (linear instruction sequence), convenient for optimization and readable
 * printing, and it leaves room for L3 (basic blocks / def-use) extensions. Later this layer can
 * host architecture-neutral abstract MIR, or add a corresponding MIR variant for other targets.
 */

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::jit::x64 {

// Invalid pc marker (not bound to any bytecode).
constexpr uint32_t kMirInvalidPc = 0xFFFFFFFFu;

// Register numbering: 0-7 = rax, rcx, rdx, rbx, r8, r9, r10, r11; 8 = rdi, 9 = rsi (ABI use).
constexpr uint8_t kRegRax = 0, kRegRcx = 1, kRegRdx = 2, kRegRbx = 3;
constexpr uint8_t kRegR8 = 4, kRegR9 = 5, kRegR10 = 6, kRegR11 = 7;
constexpr uint8_t kRegRdi = 8, kRegRsi = 9;

// Virtual register: an "unallocated" value in MIR, assigned a physical register by the unified
// register allocation stage.
using VRegId                  = uint16_t;
constexpr VRegId kInvalidVReg = 0xFFFFu;
constexpr VRegId kMaxVRegs    = 512;

inline const char *mirRegName(uint8_t r) {
    static const char *n[] = {"rax", "rcx", "rdx", "rbx", "r8", "r9", "r10", "r11", "rdi", "rsi"};
    return (r <= 9u) ? n[r] : "?";
}

// L3 extension: blockId, pred/succ, etc. can be added here.
struct MirBlockInfo {
    uint32_t blockId{0}; // 0 = unallocated; used by L3 for CFG.
};

// Keep only ABI / control flow; everything else is V* (virtual registers), allocated uniformly by
// linearScanVReg.
enum class MirOp : uint8_t {
    MovRegReg,
    MovRegImm32,
    MovRegImm64,
    PushRdi, // Save rdi (slot base) across trampoline calls; trampoline will clobber rdi.
    PopRdi,  // Restore rdi.
    PushRsi,
    PopRsi,
    PushRbx,
    PopRbx,
    SubRsp8,
    AddRsp8,
    CallRax,
    CallRel32,
    JzRel32,
    JmpRel32,
    JleRel32,
    JlRel32,
    JgRel32,
    JgeRel32,
    JeRel32,
    JneRel32,
    VCmpRegImm, // cmp vreg(r0), imm32 — sets flags, no result vreg
    JmpRel8,
    JleRel8,
    Ret, // No operands, used by TAIL, etc.
    JmpRax,
    // --- V*: r0/r1 = VRegId; for three-operand forms, the right operand lives in imm32 (vreg id)
    // ---
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
    VAddImm,      // r0 = r1 + imm32 (signed)
    VSubImm,      // r0 = r1 - imm32 (signed)
    VCmpSetLImm,  // r0 = (r1 < imm32) ? 1 : 0
    VCmpSetLEImm, // r0 = (r1 <= imm32) ? 1 : 0
    VCmpSetGImm,  // r0 = (r1 > imm32) ? 1 : 0
    VCmpSetGEImm, // r0 = (r1 >= imm32) ? 1 : 0
    VCmpSetEImm,  // r0 = (r1 == imm32) ? 1 : 0
    VCmpSetNEImm, // r0 = (r1 != imm32) ? 1 : 0
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
    DebugTrace, // Debug use: call jitDebugTrace(ctx) and print GPR + pc; inserted only in Debug
                // builds
    NativeJitFuncCall, // FUNC inline frame management + call rel32/rax; imm64 =
                       // NativeJitCallParams*
    Nop,
};

struct Mir {
    MirOp op{MirOp::Nop};
    uint8_t r0{0};
    uint8_t r1{0};
    int32_t disp{0};
    uint32_t imm32{0};
    uint64_t imm64{0};
    uint32_t pc{
        kMirInvalidPc}; // If this is the first instruction for a bytecode, set pc for pcToOffset.
    MirBlockInfo block; // L3 extension

    bool hasPc() const { return pc != kMirInvalidPc; }
    void clearPc() { pc = kMirInvalidPc; }
};

struct NativeJitCallParams {
    uint64_t poolTopAddr;            // &FramePool::top_
    uint64_t targetRuntimeGraphAddr; // Target GIR::Graph* (frame reuse check).
    uint64_t slowPathFnAddr; // Same graph: &directSelfFuncInvoke; cross graph: trampolineFunc
    uint64_t slowPathBcAddr; // &bc (for same-graph slow path)
    uint32_t slowPathPc;     // Bytecode pc (for cross-graph slow path)
    int32_t resultDisp;      // [rdi + resultDisp]
    uint8_t argsCnt;
    int32_t argSrcDisps[8];
    uint8_t argVRegs[8]; // VReg IDs for pre-loaded args (frameless path); 0xFF = not pre-loaded
    uint8_t resultVReg = 0xFF; // VReg for call result (frameless); 0xFF = store inside call
    bool isSameGraph;          // true = call rel32, false = load fn from extra2 and call rax
    uint64_t extra2Addr;       // &bc->extra2() (load fn pointer at runtime for cross graph)
    uint64_t fastop1Addr;      // &bc->fastop[1] (cross-graph runtime check: 0 = already compiled)
    bool frameless; // true = stack allocation (no Frame pool), suitable for pure self-recursive
                    // same-graph calls
    uint32_t calleeSlotBytes; // Callee stack-frame size when frameless (16-byte aligned)
};

using MirBuffer = std::vector<Mir>;

// Readable string, convenient for debug printing (for example "mov rdi, rcx").
std::string mirToString(const Mir &m);

// MIR print options: show pc only for the first instruction of each pc; leave the rest blank.
// Optional symbol and slot-name annotations. vregAlloc accepts camel::jit::VRegAllocation* (caller
// includes regalloc.h); void* is used here to avoid making mir.h depend on regalloc.
struct MirPrintOptions {
    const std::unordered_map<size_t, size_t> *pcToOffset = nullptr;
    const std::unordered_map<uint64_t, std::string> *symbolNames =
        nullptr; // address -> function / symbol name
    const std::unordered_map<int, std::string> *slotNames =
        nullptr; // disp(rdi+) -> slot name such as "n"
    const void *vregAlloc = nullptr;
};

// Print the whole buffer to a stream, format: "[pc][idx]  insn  ; comment". The pc column only
// shows the first MIR for that pc; the rest stay blank.
void mirPrint(const MirBuffer &buf, std::ostream &out, const MirPrintOptions &opts = {});

// Number of bytes encoded by a single MIR (used to compute pcToOffset after optimization)
size_t mirSizeBytes(const Mir &m);

// Total number of bytes in the whole buffer
inline size_t mirBufferSizeBytes(const MirBuffer &buf) {
    size_t n = 0;
    for (const auto &m : buf)
        n += mirSizeBytes(m);
    return n;
}

} // namespace camel::jit::x64
