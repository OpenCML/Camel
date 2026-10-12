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
 * Updated: May. 01, 2026
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
    NativeJitFuncCall, // Native self call on a stack-allocated frame; imm64 =
                       // NativeJitCallParams*
    // Arguments of a self tail call that do not fit in registers are staged on the machine
    // stack: every source is pushed, then every destination popped, so a destination that is
    // also a later source is read before it is overwritten.
    PushFrameSlot, // push qword [rdi + disp]
    PushImm64,     // push imm64 (through the scratch register)
    PopFrameSlot,  // pop qword [rdi + disp]
    // SysV AMD64 keeps jitCtx in r13 across helper calls because rsi is caller-saved there.
    // Win64 can encode this as a nop because rsi is already callee-saved.
    MovRsiR13,
    Nop,
};

// Faults compiled code can leave with. Compiled code calls no C++ and has no unwind
// information, so it cannot throw: a fault site stores the kind and its bytecode pc in the
// JitContext, restores the stack pointer its entry wrapper saved and returns to the wrapper's
// C++ caller, which raises the located diagnostic (FastVMSchedPass::invokeOwnedJitFrame).
enum class JitFaultKind : uint32_t {
    None                      = 0,
    DivisionByZero            = 1,
    MaxRecursionDepthExceeded = 2,
    // A native call refused because the machine stack of the thread is nearly full, before the
    // count reached the limit: the same diagnostic the interpreters give for their nested calls.
    NativeStackExhausted = 3,
};

// Where compiled code finds the fields of JitContext (runtime/trampoline.h asserts them).
constexpr int32_t kJitCtxFaultKindOffset   = 16;
constexpr int32_t kJitCtxFaultPcOffset     = 20;
constexpr int32_t kJitCtxDepthBudgetOffset = 24;
constexpr int32_t kJitCtxStackLimitOffset  = 32;

// A native self call puts a frame of the graph on the machine stack, up to the interpreter's
// recursion limit deep. A graph whose frame is larger than this is left to the interpreter, so
// that a recursion at the limit stays within about half a megabyte of stack.
constexpr uint32_t kMaxNativeFrameBytes = 2048;

struct Mir {
    MirOp op{MirOp::Nop};
    // Physical register ids for the fixed-register ops, virtual register ids for V* ops.
    uint16_t r0{0};
    uint16_t r1{0};
    int32_t disp{0};
    uint32_t imm32{0};
    uint64_t imm64{0};
    uint32_t pc{
        kMirInvalidPc}; // If this is the first instruction for a bytecode, set pc for pcToOffset.
    MirBlockInfo block; // L3 extension

    bool hasPc() const { return pc != kMirInvalidPc; }
    void clearPc() { pc = kMirInvalidPc; }
};

// A call of the graph being compiled to itself. The callee's frame is allocated on the machine
// stack (no frame pool), the arguments are written to its parameter slots and the body is
// entered at its JIT-internal entry.
struct NativeJitCallParams {
    // One entry per argument, in the order of the bytecode's direct-call layout.
    struct Arg {
        int32_t dstDisp = 0;            // [callee base + dstDisp]: the parameter's slot
        VRegId vreg     = kInvalidVReg; // loaded before the call; else copied inside it from:
        int32_t srcDisp = 0;            //   [rdi + srcDisp] in the caller, or
        bool isImm      = false;        //   a constant of the static area,
        uint64_t imm    = 0;            //   with this value
    };
    std::vector<Arg> args;
    int32_t resultDisp = 0;            // [rdi + resultDisp]
    VRegId resultVReg  = kInvalidVReg; // Call result; kInvalidVReg = stored to resultDisp inside
    uint32_t calleeSlotBytes = 0;      // Size of the callee's frame (16-byte aligned)
    uint32_t pc              = 0;      // Bytecode pc of the call
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
