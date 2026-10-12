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
 * Updated: May. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "x64_backend.h"

#include "../../mir/mir.h"
#include "../../mir/mir_builder.h"
#include "../../mir/mir_encode.h"
#include "../../mir/mir_optimize.h"
#include "../../regalloc/regalloc.h"
#include "../../runtime/jit_debug_trace.h"
#include "../../runtime/trampoline.h"
#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace camel::jit {

namespace {

// Emit VLoadImm32/64 for a compile-time-known static slot value
inline void emitLoadStaticConst(
    const slot_t *staticBase, data_idx_t idx, x64::VRegId v, x64::MirBuilder &build) {
    slot_t value = staticBase[static_cast<size_t>(-idx)];
    if (value <= UINT32_MAX)
        build.emitVLoadImm32(v, static_cast<uint32_t>(value));
    else
        build.emitVLoadImm64(v, value);
}

// Check if a static operand is a small int64 constant that fits in imm32
inline bool
tryGetStaticImm32(const Bytecode &bc, int opIdx, const slot_t *staticBase, int32_t &outImm) {
    if (bc.fastop[opIdx] > 0)
        return false;
    slot_t value = staticBase[static_cast<size_t>(-bc.fastop[opIdx])];
    auto sv      = static_cast<int64_t>(value);
    if (sv >= INT32_MIN && sv <= INT32_MAX) {
        outImm = static_cast<int32_t>(sv);
        return true;
    }
    return false;
}

inline void storeResultFromVReg(int disp, x64::VRegId v, x64::MirBuilder &build) {
    build.emitVStoreToFrame(disp, v);
}

inline void loadFloatOperandToVReg(
    const Bytecode &bc, int opIdx, int disp, const slot_t *staticBase, x64::VRegId v,
    x64::MirBuilder &build) {
    if (bc.fastop[opIdx] > 0)
        build.emitVXmm32LoadFromFrame(v, disp);
    else
        emitLoadStaticConst(staticBase, bc.fastop[opIdx], v, build);
}
inline void storeFloatResultFromVReg(int disp, x64::VRegId v, x64::MirBuilder &build) {
    build.emitVXmm32StoreToFrame(disp, v);
}

// Whether the divisor (operand 1) of an integer division is a constant other than 0 and -1: such
// a division needs neither the zero check nor the -1 case.
inline bool
isTrapFreeConstDivisor(const Bytecode &bc, const camel::runtime::GCGraph *graph, bool is32Bit) {
    if (bc.fastop[1] >= 0 || graph == nullptr || !graph->staticArea()) {
        return false;
    }
    const slot_t value = graph->staticArea()->data()[static_cast<size_t>(-bc.fastop[1])];
    const int64_t divisor =
        is32Bit ? static_cast<int64_t>(static_cast<int32_t>(value)) : static_cast<int64_t>(value);
    return divisor != 0 && divisor != -1;
}

std::string describeFramelessSelfFuncFailure(const CompilationUnit &unit) {
    if (unit.graphLength == 0)
        return "graph bytecode span is empty";
    const size_t graphEnd = unit.entryPc + unit.graphLength;
    ASSERT(
        graphEnd <= unit.bytecodes.size(),
        "Current graph bytecode range exceeds linked bytecode span.");
    for (size_t pc = unit.entryPc; pc < graphEnd;) {
        const Bytecode &bc = unit.bytecodes[pc];
        switch (bc.opcode) {
        case OpCode::OPER:
            return "pc=" + std::to_string(pc) + " contains OPER";
        case OpCode::CAST:
            return "pc=" + std::to_string(pc) + " contains CAST";
        case OpCode::COPY:
            return "pc=" + std::to_string(pc) + " contains COPY";
        case OpCode::FILL:
            return "pc=" + std::to_string(pc) + " contains FILL";
        case OpCode::CALL:
            return "pc=" + std::to_string(pc) + " contains indirect CALL";
        case OpCode::ACCS:
            return "pc=" + std::to_string(pc) + " contains ACCS";
        case OpCode::FUNC:
        case OpCode::TAIL:
            if (getFuncExtraRuntimeGraph(&bc) != unit.runtimeGraph) {
                const auto *target = getFuncExtraRuntimeGraph(&bc);
                return std::format(
                    "pc={} targets non-self graph '{}' ({:p})",
                    pc,
                    target ? target->name() : "<null>",
                    static_cast<const void *>(target));
            }
            break;
        default:
            break;
        }
        pc += bc.opsize;
    }
    return {};
}

// Camel standard slot model: every slot is one word (8 bytes); bool / 32-bit / 64-bit / pointer
// values all occupy one slot. JIT generated slot accesses must be 8 bytes.
static_assert(sizeof(slot_t) == 8, "JIT assumes one word per slot");

// JIT receives slot_t* (dynamic-area base), with base offset 0.
size_t getFrameDynamicAreaOffset() { return 0; }

void *allocExecutable(size_t size) {
#if defined(_WIN32) || defined(_WIN64)
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void *p =
        mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
}

void freeExecutable(void *p, size_t size) {
#if defined(_WIN32) || defined(_WIN64)
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, size);
#endif
}

} // namespace

X64Backend::X64Backend() { frameBaseOffset_ = getFrameDynamicAreaOffset(); }

int X64Backend::slotDisp(int idx) const {
    // One word per slot (sizeof(slot_t)==8), disp is in bytes.
    return static_cast<int>(frameBaseOffset_ + static_cast<size_t>(idx) * sizeof(slot_t));
}

void X64Backend::registerTrampoline(const char *name, void *addr) { trampolines_[name] = addr; }

std::unique_ptr<CompiledCode>
X64Backend::compile(const CompilationUnit &unit, std::string *failureReason) {
    auto result = std::make_unique<CompiledCode>();
    if (!compileBytecode(unit, result->code, failureReason))
        return nullptr;
    result->entryOffset    = 0;
    result->jitEntryOffset = jitEntryOffset_;
    return result;
}

bool X64Backend::compileBytecode(
    const CompilationUnit &unit, std::vector<uint8_t> &code, std::string *failureReason) {
    // The encoder has no spill code, so the allocation must give every value a register. Values
    // kept in registers across bytecodes (the slot cache, store-to-load forwarding) make the
    // fast code; when a block holds more of them than there are registers, the unit is lowered
    // again without them: every value then lives within the lowering of one bytecode.
    for (const bool keepValuesInRegisters : {true, false}) {
        code.clear();
        const LowerResult result =
            compileBytecodeWith(unit, code, failureReason, keepValuesInRegisters);
        if (result != LowerResult::OutOfRegisters)
            return result == LowerResult::Ok;
        CAMEL_LOG_INFO_S(
            "JIT",
            "graph '{}' is lowered again without values kept in registers: {}",
            unit.runtimeGraph ? unit.runtimeGraph->name() : "<null>",
            failureReason ? *failureReason : std::string());
    }
    return false;
}

X64Backend::LowerResult X64Backend::compileBytecodeWith(
    const CompilationUnit &unit, std::vector<uint8_t> &code, std::string *failureReason,
    bool keepValuesInRegisters) {
    using namespace x64;
    const CompilationDebugOptions *debug = unit.debug;

    // Keep failure handling local so every opcode branch can return a precise
    // bytecode PC and reason without threading error strings through helpers.
    auto fail = [&](const std::string &msg) {
        if (failureReason)
            *failureReason = msg;
        return LowerResult::Failed;
    };

    if (!unit.runtimeGraph)
        return fail("null runtime graph in JIT compilation unit");
    if (!unit.runtimeGraph->hasFrameLayout())
        return fail("incomplete frame layout for graph '" + unit.runtimeGraph->name() + "'");
    // Graphs without constants are left to the interpreter.
    if (!unit.runtimeGraph->staticArea())
        return fail("graph '" + unit.runtimeGraph->name() + "' has no static area");

    const Bytecode *base = unit.bytecodes.data();
    size_t entryPc       = unit.entryPc;
    // Only this graph's bytecode: what follows belongs to other graphs (among them graphs
    // compiled at run time, whose code may use slots and opcodes this graph never does).
    size_t pcEnd = unit.graphLength != 0
                       ? std::min(unit.bytecodes.size(), entryPc + unit.graphLength)
                       : unit.bytecodes.size();

    const slot_t *staticBase = unit.runtimeGraph->staticArea()->data();
    auto staticSlotAddr      = [&](data_idx_t idx) -> uint64_t {
        return reinterpret_cast<uint64_t>(staticBase + static_cast<size_t>(-idx));
    };
    (void)staticSlotAddr; // staticSlotAddr retained for debug use; constants now inlined

    // The backend is intentionally split into two stages:
    // 1) lower bytecode to linear MIR while keeping bytecode-PC annotations
    // 2) optimize / allocate / encode from MIR
    //
    // This keeps frontend pattern matching local in one pass while still
    // leaving enough structure for later peephole and dead-store cleanup.
    x64::MirBuffer mirBuf;
    x64::MirBuilder build(mirBuf);
    x64::VRegId nextVReg                     = 0;
    const std::string framelessFailureReason = describeFramelessSelfFuncFailure(unit);
    const bool canUseFramelessSelfFunc       = framelessFailureReason.empty();
    // The current x64 JIT is only sound for graphs whose execution stays within
    // the slot-native bytecode subset. Graphs that rely on runtime Function
    // carriers, indirect CALL, or frame-dependent trampolines still execute
    // correctly through the interpreter, while pure callee graphs such as
    // recursive fib keep the hot-path JIT speedup.
    //
    // What passes this check never calls into C++: its calls are native self calls and its
    // tail calls are jumps. The lowerings of OPER, CAST, COPY, FILL, CALL and ACCS further
    // down are therefore not reached today.
    if (!canUseFramelessSelfFunc)
        return fail("graph requires interpreter fallback: " + framelessFailureReason);

    // Arguments of a call that are kept in registers at once; a call with more copies them
    // one by one through memory (see FUNC and TAIL).
    constexpr size_t kMaxRegisterArgs = 4;
    // Owns the parameter blocks the NativeJitFuncCall MIRs point to, until encoding is done.
    std::vector<std::unique_ptr<NativeJitCallParams>> callParams;

    // Compare-Branch fusion state: when a comparison detects a following BRCH
    // using its result, it emits only VLoadFromFrame + VCmpRegImm (no setcc/store),
    // and records info so BRCH can emit a single Jcc.
    struct FusedCmpInfo {
        bool active           = false;
        OpCode cmpOp          = OpCode::RETN;
        data_idx_t resultSlot = 0; // comparison's result slot (= BRCH's condSlot)
    } fusedCmp;

    // A fused cmp-BRCH does not compute the branch index its JOIN selects by. Two ways to do
    // without it:
    // - Arm 0 is empty (its jump-table entry is the JUMP to the JOIN): that JUMP is lowered as
    //   "take w0 and leave", so whatever else reaches the JOIN comes from arm 1 and the JOIN
    //   takes w1 directly. Keyed by the index slot, because a nested fused branch in arm 1 is
    //   lowered between this BRCH and its JOIN.
    // - Otherwise an arm with code of its own reaches the JOIN, by a jump or by falling into
    //   it, depending on how the arms were laid out. The index is then written where each arm
    //   starts: at the two jump-table entries after the BRCH.
    std::unordered_set<data_idx_t> joinsTakingArm1;
    struct ArmIndexWrite {
        data_idx_t idxSlot;
        uint32_t armIndex;
    };
    std::unordered_map<size_t, ArmIndexWrite> armIndexWrites;

    // Read-only slot cache:
    // if a dynamic slot is never written by any bytecode in this function,
    // repeated loads of that slot can be replaced by VCopy from the first load.
    //
    // The cache lives for one basic block: the register is only known to hold the
    // slot where the load has run, and a load made in one arm of a branch has not
    // run in the other arm, nor on every path to the join. It is cleared wherever
    // straight-line code starts anew (blockStarts) and at calls, which clobber the
    // caller-saved regs even if the logical slot value itself is unchanged.
    std::unordered_set<data_idx_t> writtenSlots;
    for (size_t p = entryPc; p < pcEnd; p += base[p].opsize)
        if (base[p].result > 0)
            writtenSlots.insert(base[p].result);
    std::unordered_map<int, x64::VRegId> slotCache;
    // Every jump target, and the bytecode after each BRCH, JUMP, RETN and TAIL. An
    // if-else BRCH is followed by its two-entry jump table (the targets its lowering uses).
    std::unordered_set<size_t> blockStarts;
    for (size_t p = entryPc; p < pcEnd; p += base[p].opsize) {
        const size_t next = p + base[p].opsize;
        switch (base[p].opcode) {
        case OpCode::BRCH:
            blockStarts.insert(next);
            blockStarts.insert(next + 1);
            break;
        case OpCode::JUMP:
            blockStarts.insert(getJumpTargetPc(&base[p]));
            blockStarts.insert(next);
            break;
        case OpCode::RETN:
        case OpCode::TAIL:
            blockStarts.insert(next);
            break;
        default:
            break;
        }
    }

    // `cacheable` is false for a load whose code does not run on every path through the
    // current block (see the fused JOIN).
    auto loadSlot = [&](data_idx_t slotIdx, int disp, x64::VRegId v, bool cacheable = true) {
        // Positive indices address the dynamic frame, negative indices address
        // compile-time-known static slots.
        if (slotIdx > 0) {
            auto it = slotCache.find(disp);
            if (it != slotCache.end()) {
                build.emitVCopy(v, it->second);
                return;
            }
            build.emitVLoadFromFrame(v, disp);
            if (cacheable && keepValuesInRegisters && !writtenSlots.count(slotIdx))
                slotCache[disp] = v;
        } else {
            emitLoadStaticConst(staticBase, slotIdx, v, build);
        }
    };

    // Shadow free-function loadOperandToVReg so all existing call sites
    // (including LCMP_HANDLER macro) route through the cache.
    auto loadOperandToVReg = [&](const Bytecode &bc,
                                 int opIdx,
                                 int disp,
                                 const slot_t * /*sb*/,
                                 x64::VRegId v,
                                 x64::MirBuilder & /*b*/) { loadSlot(bc.fastop[opIdx], disp, v); };

    // The wrapper owns all ABI adaptation. MIR generation below always assumes
    // the internal JIT convention:
    //   rdi = current frame dynamic area
    //   rsi = JitContext*
    //   rbx = &FramePool::top_
    // Keeping the body prologue-free is what makes direct JIT-to-JIT calls cheap.

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];
        if (blockStarts.count(pc))
            slotCache.clear();
        build.setNextPc(static_cast<uint32_t>(pc));
        if (debug && debug->enableDebugTrace) {
            build.emitDebugTrace(static_cast<uint32_t>(pc));
        }
        if (auto write = armIndexWrites.find(pc); write != armIndexWrites.end()) {
            x64::VRegId vIdx = nextVReg++;
            build.emitVLoadImm32(vIdx, write->second.armIndex);
            build.emitVStoreToFrame(slotDisp(write->second.idxSlot), vIdx);
        }
        switch (bc.opcode) {
        case OpCode::LADD: {
            int dr = slotDisp(bc.result);
            int32_t imm;
            if (tryGetStaticImm32(bc, 1, staticBase, imm)) {
                x64::VRegId v0 = nextVReg++, vr = nextVReg++;
                loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);
                build.emitVAddImm(vr, v0, imm);
            } else if (tryGetStaticImm32(bc, 0, staticBase, imm)) {
                x64::VRegId v1 = nextVReg++, vr = nextVReg++;
                loadOperandToVReg(bc, 1, slotDisp(bc.fastop[1]), staticBase, v1, build);
                build.emitVAddImm(vr, v1, imm);
            } else {
                x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
                loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);
                loadOperandToVReg(bc, 1, slotDisp(bc.fastop[1]), staticBase, v1, build);
                build.emitVAdd(vr, v0, v1);
            }
            storeResultFromVReg(dr, nextVReg - 1, build);
            break;
        }
        case OpCode::LSUB: {
            int dr = slotDisp(bc.result);
            int32_t imm;
            if (tryGetStaticImm32(bc, 1, staticBase, imm)) {
                x64::VRegId v0 = nextVReg++, vr = nextVReg++;
                loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);
                build.emitVSubImm(vr, v0, imm);
            } else {
                x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
                loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);
                loadOperandToVReg(bc, 1, slotDisp(bc.fastop[1]), staticBase, v1, build);
                build.emitVSub(vr, v0, v1);
            }
            storeResultFromVReg(dr, nextVReg - 1, build);
            break;
        }
        case OpCode::DADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmAdd(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DSUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmSub(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmMul(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmDiv(vr, v0, v1);
            build.markDivision(static_cast<uint32_t>(pc), true);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DLT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmCmpSetB(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmCmpSetA(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmCmpSetE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DNE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmCmpSetNZ(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DLE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmCmpSetBE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmmCmpSetAE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVAdd32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::ISUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVSub32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVMul32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVIdiv32(vr, v0, v1);
            build.markDivision(
                static_cast<uint32_t>(pc),
                !isTrapFreeConstDivisor(bc, unit.runtimeGraph, bc.opcode == OpCode::IDIV));
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::ILT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVCmpSetL32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVCmpSetG32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVCmpSetE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::INE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVCmpSetNE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::ILE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVCmpSetLE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVCmpSetGE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32Add(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FSUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32Sub(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32Mul(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32Div(vr, v0, v1);
            build.markDivision(static_cast<uint32_t>(pc), true);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FLT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32CmpSetB(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32CmpSetA(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32CmpSetE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FNE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32CmpSetNZ(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FLE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32CmpSetBE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVXmm32CmpSetAE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVMul(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticBase, v0, build);
            loadOperandToVReg(bc, 1, d1, staticBase, v1, build);
            build.emitVIdiv(vr, v0, v1);
            build.markDivision(
                static_cast<uint32_t>(pc),
                !isTrapFreeConstDivisor(bc, unit.runtimeGraph, bc.opcode == OpCode::IDIV));
            storeResultFromVReg(dr, vr, build);
            break;
        }
        // --- L-comparison handlers with BRCH fusion ---
        // Look ahead: if the next BC is BRCH using our result, emit bare cmp + flags only.
#define LCMP_HANDLER(OPCODE, CmpSetImm, CmpSetReg)                                                 \
    case OpCode::OPCODE: {                                                                         \
        int dr        = slotDisp(bc.result);                                                       \
        size_t nextPc = pc + bc.opsize;                                                            \
        bool fuse     = false;                                                                     \
        int32_t imm;                                                                               \
        if (nextPc < pcEnd && base[nextPc].opcode == OpCode::BRCH &&                               \
            base[nextPc].withCnt() == 0 && base[nextPc].nargs()[0] == bc.result &&                 \
            tryGetStaticImm32(bc, 1, staticBase, imm)) {                                           \
            fuse           = true;                                                                 \
            x64::VRegId v0 = nextVReg++;                                                           \
            loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);               \
            build.emitVCmpRegImm(v0, imm);                                                         \
            fusedCmp = {true, OpCode::OPCODE, bc.result};                                          \
        }                                                                                          \
        if (!fuse) {                                                                               \
            if (tryGetStaticImm32(bc, 1, staticBase, imm)) {                                       \
                x64::VRegId v0 = nextVReg++, vr = nextVReg++;                                      \
                loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);           \
                build.emit##CmpSetImm(vr, v0, imm);                                                \
            } else {                                                                               \
                x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;                     \
                loadOperandToVReg(bc, 0, slotDisp(bc.fastop[0]), staticBase, v0, build);           \
                loadOperandToVReg(bc, 1, slotDisp(bc.fastop[1]), staticBase, v1, build);           \
                build.emit##CmpSetReg(vr, v0, v1);                                                 \
            }                                                                                      \
            storeResultFromVReg(dr, nextVReg - 1, build);                                          \
        }                                                                                          \
        break;                                                                                     \
    }
            LCMP_HANDLER(LLE, VCmpSetLEImm, VCmpSetLE)
            LCMP_HANDLER(LLT, VCmpSetLImm, VCmpSetL)
            LCMP_HANDLER(LGT, VCmpSetGImm, VCmpSetG)
            LCMP_HANDLER(LGE, VCmpSetGEImm, VCmpSetGE)
            LCMP_HANDLER(LEQ, VCmpSetEImm, VCmpSetE)
            LCMP_HANDLER(LNE, VCmpSetNEImm, VCmpSetNE)
#undef LCMP_HANDLER
        case OpCode::BRCH: {
            if (bc.withCnt() != 0)
                return fail(
                    "pc=" + std::to_string(pc) +
                    " BRCH only supports simple if-else (withCnt=0); match-case (withCnt>0) not "
                    "yet implemented, got " +
                    std::to_string(bc.withCnt()));
            int dr    = slotDisp(bc.result);
            size_t t0 = pc + bc.opsize;
            size_t t1 = pc + bc.opsize + 1;
            if (fusedCmp.active) {
                // Fused compare-branch: CPU flags already set by preceding cmp. The branch
                // index is not computed here; see joinsTakingArm1 / armIndexWrites.
                size_t joinPc = pcEnd;
                for (size_t p = t1 + 1; p < pcEnd; p += base[p].opsize) {
                    if (base[p].opcode == OpCode::JOIN && base[p].normCnt() > 0 &&
                        base[p].nargs()[0] == bc.result) {
                        joinPc = p;
                        break;
                    }
                }
                const bool arm0Empty = joinPc < pcEnd && base[joinPc].withCnt() == 2 &&
                                       base[t0].opcode == OpCode::JUMP &&
                                       getJumpTargetPc(&base[t0]) == joinPc;
                if (arm0Empty) {
                    joinsTakingArm1.insert(bc.result);
                } else if (bc.result > 0) {
                    armIndexWrites[t0] = {bc.result, 0};
                    armIndexWrites[t1] = {bc.result, 1};
                }
                // Emit inverted jcc: jump to else (t1) when condition is NOT met
                auto u32t1 = static_cast<uint32_t>(t1);
                switch (fusedCmp.cmpOp) {
                case OpCode::LLE:
                    build.emitJgRel32(u32t1);
                    break;
                case OpCode::LLT:
                    build.emitJgeRel32(u32t1);
                    break;
                case OpCode::LGT:
                    build.emitJleRel32(u32t1);
                    break;
                case OpCode::LGE:
                    build.emitJlRel32(u32t1);
                    break;
                case OpCode::LEQ:
                    build.emitJneRel32(u32t1);
                    break;
                case OpCode::LNE:
                    build.emitJeRel32(u32t1);
                    break;
                default:
                    break;
                }
                // Fall through to t0 (then body)
                fusedCmp.active = false;
            } else {
                // Generic lowering keeps BRCH as a data-flow construct:
                // materialize the branch index into the result slot, then emit
                // the actual branch. Later JOIN may simplify this pattern.
                data_idx_t condIdx = bc.nargs()[0];
                if (condIdx <= 0)
                    return fail(
                        "pc=" + std::to_string(pc) +
                        " BRCH cond slot index must be positive, got " + std::to_string(condIdx));
                int dc            = slotDisp(condIdx);
                x64::VRegId vCond = nextVReg++;
                x64::VRegId vZero = nextVReg++;
                x64::VRegId vOne  = nextVReg++;
                x64::VRegId vRes  = nextVReg++;
                if (condIdx > 0)
                    build.emitVLoadFromFrame(vCond, dc);
                else
                    emitLoadStaticConst(staticBase, condIdx, vCond, build);
                build.emitVLoadImm32(vZero, 0);
                build.emitVLoadImm32(vOne, 1);
                build.emitVCopy(vRes, vOne);
                build.emitVTest(vCond);
                build.emitVCmovnz(vRes, vZero);
                build.emitVStoreToFrame(dr, vRes);
                build.emitJzRel32(static_cast<uint32_t>(t1));
                build.emitJmpRel32(static_cast<uint32_t>(t0));
            }
            break;
        }
        case OpCode::JOIN: {
            if (bc.result == 0) {
                break;
            }
            if (bc.extra()->pType == camel::core::type::Type::Void()) {
                x64::VRegId vNull = nextVReg++;
                build.emitVLoadImm64(vNull, 0);
                build.emitVStoreToFrame(slotDisp(bc.result), vNull);
                break;
            }
            if (bc.withCnt() != 2)
                return fail(
                    "pc=" + std::to_string(pc) +
                    " JOIN only supports withCnt=2 (if-else); match-case (withCnt>2) not yet "
                    "implemented, got " +
                    std::to_string(bc.withCnt()));
            data_idx_t idxSlot = bc.nargs()[0];
            data_idx_t w0      = bc.wargs()[0];
            data_idx_t w1      = bc.wargs()[1];
            int d1 = slotDisp(w1), dr = slotDisp(bc.result);

            if (joinsTakingArm1.count(idxSlot)) {
                // Arm 0 of a fused cmp-BRCH left through its JUMP (see joinsTakingArm1), so
                // this code runs for arm 1 only: take w1 directly. Arm 0 jumps past this
                // code, so what follows the JOIN must not reuse the load.
                x64::VRegId v1 = nextVReg++;
                loadSlot(w1, d1, v1, false);

                // Phase N: if JOIN is immediately followed by RETN reading
                // JOIN's result, emit VRet directly — skip the intermediate store.
                size_t nextPc = pc + bc.opsize;
                if (nextPc < pcEnd && base[nextPc].opcode == OpCode::RETN &&
                    base[nextPc].fastop[0] == bc.result && !blockStarts.count(nextPc)) {
                    build.emitVRet(v1);
                    // Advance pc so the loop's `pc += bc.opsize` lands past RETN.
                    pc = nextPc + base[nextPc].opsize - bc.opsize;
                } else {
                    build.emitVStoreToFrame(dr, v1);
                }
            } else {
                // Generic JOIN is lowered as a select: start with w0, then
                // overwrite with w1 when idx != 0. The extra frame disp passed
                // to VTest lets the encoder recover a frame-based condition even
                // when idx itself is spilled.
                int dIdx = slotDisp(idxSlot), d0 = slotDisp(w0);
                x64::VRegId v0 = nextVReg++;
                x64::VRegId v1 = nextVReg++;
                x64::VRegId v2 = nextVReg++;
                x64::VRegId v3 = nextVReg++;
                loadSlot(w0, d0, v0);
                loadSlot(w1, d1, v1);
                loadSlot(idxSlot, dIdx, v2);
                build.emitVCopy(v3, v0);
                build.emitVTest(v2);
                build.emitVCmovnz(v3, v1);
                build.emitVStoreToFrame(dr, v3);
            }
            break;
        }
        case OpCode::FUNC: {
            // Every call in a graph that gets here is a call to the graph itself
            // (describeFramelessSelfFuncFailure). It is encoded natively, in the JIT's own
            // convention on every ABI: the callee's frame is allocated on the machine stack
            // and the body is entered directly, with no frame pool and no C++ in between.
            if (getFuncExtraRuntimeGraph(&bc) != unit.runtimeGraph)
                return fail("pc=" + std::to_string(pc) + " FUNC targets another graph");
            const data_arr_t srcArgs  = bc.directCallSrcArgs();
            const data_arr_t dstSlots = bc.directCallDstSlots();
            if (srcArgs.size() != dstSlots.size())
                return fail("pc=" + std::to_string(pc) + " FUNC layout is arity-mismatched");

            callParams.push_back(std::make_unique<NativeJitCallParams>());
            NativeJitCallParams *params = callParams.back().get();
            params->pc                  = static_cast<uint32_t>(pc);
            params->resultDisp          = slotDisp(bc.result);
            const size_t slotCount      = unit.runtimeGraph->runtimeDataType()->size();
            params->calleeSlotBytes =
                static_cast<uint32_t>((slotCount * sizeof(slot_t) + 15u) & ~size_t{15});
            // Phase L: the arguments of a call with few of them are loaded by visible
            // VLoadFromFrame, so that peephole can fuse a preceding store with the load.
            const bool inRegisters = srcArgs.size() <= kMaxRegisterArgs;
            for (size_t ai = 0; ai < srcArgs.size(); ++ai) {
                NativeJitCallParams::Arg arg;
                arg.dstDisp = slotDisp(dstSlots[ai]);
                if (srcArgs[ai] <= 0) {
                    arg.isImm = true;
                    arg.imm   = staticBase[static_cast<size_t>(-srcArgs[ai])];
                } else {
                    arg.srcDisp = slotDisp(srcArgs[ai]);
                    if (inRegisters) {
                        arg.vreg = nextVReg++;
                        loadSlot(srcArgs[ai], arg.srcDisp, arg.vreg);
                    }
                }
                params->args.push_back(arg);
            }
            // Phase Q: the result is a vreg with a visible store, so that peephole can fuse
            // the subsequent VLoadFromFrame into a VCopy.
            params->resultVReg = nextVReg++;
            build.emitNativeJitFuncCall(params);
            build.emitVStoreToFrame(params->resultDisp, params->resultVReg);
            // Calls are the main cache barrier: no register holds a slot value after one.
            slotCache.clear();
            break;
        }
        case OpCode::TAIL: {
            // A self tail call is "rewrite the parameter slots and jump to the entry", on
            // every ABI: no call instruction, no new frame, the recursion stays in the current
            // activation, whether its frame came from the pool or is on the machine stack.
            if (getFuncExtraRuntimeGraph(&bc) != unit.runtimeGraph)
                return fail("pc=" + std::to_string(pc) + " TAIL targets another graph");
            const data_arr_t srcArgs  = bc.directCallSrcArgs();
            const data_arr_t dstSlots = bc.directCallDstSlots();
            if (srcArgs.size() != dstSlots.size())
                return fail("pc=" + std::to_string(pc) + " TAIL layout is arity-mismatched");
            // An argument that is the parameter itself stays where it is.
            std::vector<size_t> moved;
            for (size_t i = 0; i < srcArgs.size(); ++i) {
                if (srcArgs[i] != dstSlots[i])
                    moved.push_back(i);
            }
            // All sources are read before any parameter slot is written: an argument may be
            // another parameter (`f(b, a)`).
            if (moved.size() <= kMaxRegisterArgs) {
                std::vector<x64::VRegId> argRegs;
                for (const size_t i : moved) {
                    argRegs.push_back(nextVReg++);
                    loadSlot(srcArgs[i], slotDisp(srcArgs[i]), argRegs.back());
                }
                for (size_t k = 0; k < moved.size(); ++k)
                    build.emitVStoreToFrame(slotDisp(dstSlots[moved[k]]), argRegs[k]);
            } else {
                for (const size_t i : moved) {
                    if (srcArgs[i] > 0)
                        build.emitPushFrameSlot(slotDisp(srcArgs[i]));
                    else
                        build.emitPushImm64(staticBase[static_cast<size_t>(-srcArgs[i])]);
                }
                for (size_t k = moved.size(); k-- > 0;)
                    build.emitPopFrameSlot(slotDisp(dstSlots[moved[k]]));
            }
            build.emitJmpRel32(static_cast<uint32_t>(entryPc));
            break;
        }
        case OpCode::JUMP: {
            size_t target = getJumpTargetPc(&bc);
            // If the target is JOIN and the current block is the BRCH "first branch" (only this
            // JUMP), write back w0 directly and jump to JOIN afterward, to avoid reading an
            // uninitialized w1 through JOIN.
            bool isFirstBranchToJoin = false;
            if (target < pcEnd && base[target].opcode == OpCode::JOIN) {
                for (size_t p = entryPc; p < pc; p += base[p].opsize) {
                    if (base[p].opcode == OpCode::BRCH && p + base[p].opsize == pc) {
                        isFirstBranchToJoin = true;
                        break;
                    }
                }
            }
            if (isFirstBranchToJoin) {
                const Bytecode &joinBc = base[target];
                if (joinBc.withCnt() >= 1) {
                    int d0 = slotDisp(joinBc.wargs()[0]);

                    // Phase M: if JOIN is immediately followed by RETN reading
                    // JOIN's result, emit VRet directly instead of store+jmp.
                    size_t afterJoin = target + joinBc.opsize;
                    if (afterJoin < pcEnd && base[afterJoin].opcode == OpCode::RETN &&
                        base[afterJoin].fastop[0] == joinBc.result) {
                        // Phase O+M: if the slot is already in the cache, emit
                        // VRet(cached) directly — avoids a VCopy that the
                        // allocator might assign to a different register.
                        if (joinBc.wargs()[0] > 0) {
                            auto cacheIt = slotCache.find(d0);
                            if (cacheIt != slotCache.end()) {
                                build.emitVRet(cacheIt->second);
                                break;
                            }
                        }
                        x64::VRegId v0 = nextVReg++;
                        loadSlot(joinBc.wargs()[0], d0, v0);
                        build.emitVRet(v0);
                    } else {
                        x64::VRegId v0 = nextVReg++;
                        loadSlot(joinBc.wargs()[0], d0, v0);
                        int dr = slotDisp(joinBc.result);
                        build.emitVStoreToFrame(dr, v0);
                        build.emitJmpRel32(static_cast<uint32_t>(target + joinBc.opsize));
                    }
                    break;
                }
            }
            // Fallback: keep the bytecode CFG shape and let the MIR encoder patch
            // the final relative offset after the whole buffer is laid out.
            build.emitJmpRel32(static_cast<uint32_t>(target));
            break;
        }
        case OpCode::RETN: {
            x64::VRegId v0 = nextVReg++;
            if (bc.fastop[0] == 0) {
                build.emitVLoadImm64(v0, 0);
            } else {
                int d0 = slotDisp(bc.fastop[0]);
                loadSlot(bc.fastop[0], d0, v0);
            }
            build.emitVRet(v0);
            break;
        }
        case OpCode::OPER: {
            if (!unit.trampolineOper)
                return fail("pc=" + std::to_string(pc) + " no OPER trampoline");
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineOper);
#if defined(_WIN32) || defined(_WIN64)
            build.emitCallTrampolineOperWin64(static_cast<uint32_t>(pc), addr);
#else
            build.emitCallTrampolineOperSysV(static_cast<uint32_t>(pc), addr);
#endif
            x64::VRegId vRet = nextVReg++;
            build.emitVMovFromRax(vRet);
            build.emitVStoreToFrame(slotDisp(bc.result), vRet);
            slotCache.clear();
            break;
        }
        case OpCode::CAST: {
            if (!unit.trampolineCast)
                return fail("pc=" + std::to_string(pc) + " no CAST trampoline");
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineCast);
#if defined(_WIN32) || defined(_WIN64)
            build.emitCallTrampolineOperWin64(static_cast<uint32_t>(pc), addr);
#else
            build.emitCallTrampolineOperSysV(static_cast<uint32_t>(pc), addr);
#endif
            x64::VRegId vRet = nextVReg++;
            build.emitVMovFromRax(vRet);
            build.emitVStoreToFrame(slotDisp(bc.result), vRet);
            slotCache.clear();
            break;
        }
        case OpCode::COPY:
        case OpCode::FILL:
        case OpCode::CALL:
        case OpCode::ACCS: {
            if (!unit.trampolineBytecode) {
                return fail("pc=" + std::to_string(pc) + " no generic bytecode trampoline");
            }
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineBytecode);
#if defined(_WIN32) || defined(_WIN64)
            build.emitCallTrampolineOperWin64(static_cast<uint32_t>(pc), addr);
#else
            build.emitCallTrampolineOperSysV(static_cast<uint32_t>(pc), addr);
#endif
            x64::VRegId vRet = nextVReg++;
            build.emitVMovFromRax(vRet);
            build.emitVStoreToFrame(slotDisp(bc.result), vRet);
            slotCache.clear();
            break;
        }
        default:
            return fail("pc=" + std::to_string(pc) + " unsupported opcode " + to_string(bc.opcode));
        }

        pc += bc.opsize;
    }

    // rmir: MIR obtained directly from bytecode, unoptimized; print and return.
    if (debug && debug->mirOut && debug->mirSlotOnly) {
        std::unordered_map<size_t, size_t> pcToOffset;
        size_t offset = 0;
        for (const auto &mi : mirBuf) {
            if (mi.hasPc())
                pcToOffset[static_cast<size_t>(mi.pc)] = offset;
            offset += x64::mirSizeBytes(mi);
        }
        x64::MirPrintOptions opts;
        opts.pcToOffset  = &pcToOffset;
        opts.symbolNames = debug->mirSymbolNames;
        opts.slotNames   = debug->mirSlotNames;
        opts.vregAlloc   = nullptr;
        x64::mirPrint(mirBuf, *debug->mirOut, opts);
        return LowerResult::Ok;
    }

    // Run MIR cleanup before allocation. These passes still operate on virtual
    // registers, so they can rewrite loads/stores without worrying about
    // physical register side effects.
    x64::runMirOptimizationPasses(mirBuf, keepValuesInRegisters);

    // mir: optimized vreg MIR; print and return (do not allocate or encode).
    if (debug && debug->mirOut) {
        std::unordered_map<size_t, size_t> pcToOffset;
        size_t offset = 0;
        for (const auto &mi : mirBuf) {
            if (mi.hasPc())
                pcToOffset[static_cast<size_t>(mi.pc)] = offset;
            offset += x64::mirSizeBytes(mi);
        }
        x64::MirPrintOptions opts;
        opts.pcToOffset  = &pcToOffset;
        opts.symbolNames = debug->mirSymbolNames;
        opts.slotNames   = debug->mirSlotNames;
        opts.vregAlloc   = nullptr;
        x64::mirPrint(mirBuf, *debug->mirOut, opts);
        return LowerResult::Ok;
    }

    VRegAllocation vregAlloc;
    std::string allocFailure;
    if (!linearScanVReg(mirBuf, &vregAlloc, &allocFailure)) {
        if (failureReason)
            *failureReason = "register allocation failed: " + allocFailure;
        return keepValuesInRegisters ? LowerResult::OutOfRegisters : LowerResult::Failed;
    }
    void *debugTraceFn =
#ifndef NDEBUG
        ((debug && debug->enableDebugTrace) ? reinterpret_cast<void *>(&jitDebugTraceWrapper)
                                            : nullptr);
#else
        nullptr;
#endif

    // Generate C++ ABI wrapper, then call MIR body. C++ enters at offset 0; JIT-to-JIT uses
    // jitEntryOffset_ (body start) so rbx/r12 pool+graph cache is already live.
    //
    // Windows: rcx/rdx → rdi/rsi + load rbx/r12 (MIR/regalloc assume these on body entry).
    // Linux SysV: rdi/rsi already hold slots/ctx; historically only Windows emitted this prologue,
    // so Linux ran the body with garbage rbx/r12 and mis-compared / mis-loaded frame slots.
    // A fault site in the body leaves through the wrapper: it restores the stack pointer saved
    // in r14 (callee-saved, untouched by the body) and jumps to the wrapper's epilogue, which
    // returns to the C++ caller. Nothing else is between the two: the body calls only itself.
    size_t wrapperSize     = 0;
    size_t faultExitOffset = 0;
#if defined(_WIN32) || defined(_WIN64)
    {
        uint64_t poolAddr  = unit.poolTopAddr ? reinterpret_cast<uint64_t>(unit.poolTopAddr) : 0;
        uint64_t graphAddr = reinterpret_cast<uint64_t>(unit.runtimeGraph);
        // push rdi; push rsi; push rbx; push r12  (4 pushes → RSP 8→0→8→0→8 mod 16)
        // 4 pushes gives correct Win64 alignment: body RSP ≡ 0 (mod 16) after wrapper's call.
        code.push_back(0x57);                  // push rdi
        code.push_back(0x56);                  // push rsi
        code.push_back(0x53);                  // push rbx
        code.insert(code.end(), {0x41, 0x54}); // push r12
        code.insert(code.end(), {0x41, 0x56}); // push r14
        code.insert(code.end(), {0x41, 0x57}); // push r15 (keeps the body's stack alignment)
        // mov rdi, rcx  (Win64 arg1 → JIT slot base)
        code.insert(code.end(), {0x48, 0x89, 0xCF});
        // mov rsi, rdx  (Win64 arg2 → JIT ctx)
        code.insert(code.end(), {0x48, 0x89, 0xD6});
        // mov rbx, imm64(poolTopAddr)  — cached &FramePool::top_
        code.push_back(0x48);
        code.push_back(0xBB);
        for (int b = 0; b < 8; ++b)
            code.push_back(static_cast<uint8_t>((poolAddr >> (b * 8)) & 0xFF));
        // mov r12, imm64(graphAddr)  — cached current Graph* for fast frame acquire
        code.push_back(0x49);
        code.push_back(0xBC);
        for (int b = 0; b < 8; ++b)
            code.push_back(static_cast<uint8_t>((graphAddr >> (b * 8)) & 0xFF));
        // mov r14, rsp  — what a fault exit restores before it continues at the epilogue
        code.insert(code.end(), {0x49, 0x89, 0xE6});
        // call rel32(jitBody)
        code.push_back(0xE8);
        size_t callPatchPos = code.size();
        code.insert(code.end(), {0, 0, 0, 0});
        faultExitOffset = code.size();
        // pop r15; pop r14; pop r12; pop rbx; pop rsi; pop rdi; ret
        code.insert(code.end(), {0x41, 0x5F}); // pop r15
        code.insert(code.end(), {0x41, 0x5E}); // pop r14
        code.insert(code.end(), {0x41, 0x5C}); // pop r12
        code.push_back(0x5B);                  // pop rbx
        code.push_back(0x5E);                  // pop rsi
        code.push_back(0x5F);                  // pop rdi
        code.push_back(0xC3);                  // ret
        wrapperSize            = code.size();
        int32_t callRel        = static_cast<int32_t>(wrapperSize - (callPatchPos + 4));
        code[callPatchPos]     = static_cast<uint8_t>(callRel & 0xFF);
        code[callPatchPos + 1] = static_cast<uint8_t>((callRel >> 8) & 0xFF);
        code[callPatchPos + 2] = static_cast<uint8_t>((callRel >> 16) & 0xFF);
        code[callPatchPos + 3] = static_cast<uint8_t>((callRel >> 24) & 0xFF);
    }
#else
    // System V AMD64: slot_t * in rdi, void *ctx in rsi (same as JIT body convention).
    // r13 keeps a stable copy of jitCtx because helper calls are free to clobber rsi under SysV.
    {
        uint64_t poolAddr  = unit.poolTopAddr ? reinterpret_cast<uint64_t>(unit.poolTopAddr) : 0;
        uint64_t graphAddr = reinterpret_cast<uint64_t>(unit.runtimeGraph);
        code.push_back(0x53);                        // push rbx (callee-saved)
        code.insert(code.end(), {0x41, 0x54});       // push r12
        code.insert(code.end(), {0x41, 0x55});       // push r13
        code.insert(code.end(), {0x41, 0x56});       // push r14
        code.insert(code.end(), {0x41, 0x57}); // push r15 (keeps the body's stack alignment)
        code.insert(code.end(), {0x49, 0x89, 0xF5}); // mov r13, rsi
        // mov rbx, imm64(poolTopAddr)
        code.push_back(0x48);
        code.push_back(0xBB);
        for (int b = 0; b < 8; ++b)
            code.push_back(static_cast<uint8_t>((poolAddr >> (b * 8)) & 0xFF));
        // mov r12, imm64(graphAddr)
        code.push_back(0x49);
        code.push_back(0xBC);
        for (int b = 0; b < 8; ++b)
            code.push_back(static_cast<uint8_t>((graphAddr >> (b * 8)) & 0xFF));
        // mov r14, rsp  — what a fault exit restores before it continues at the epilogue
        code.insert(code.end(), {0x49, 0x89, 0xE6});
        code.push_back(0xE8);
        size_t callPatchPos = code.size();
        code.insert(code.end(), {0, 0, 0, 0});
        faultExitOffset = code.size();
        code.insert(code.end(), {0x41, 0x5F}); // pop r15
        code.insert(code.end(), {0x41, 0x5E}); // pop r14
        code.insert(code.end(), {0x41, 0x5D}); // pop r13
        code.insert(code.end(), {0x41, 0x5C}); // pop r12
        code.push_back(0x5B);                  // pop rbx
        code.push_back(0xC3);                  // ret
        wrapperSize            = code.size();
        int32_t callRel        = static_cast<int32_t>(wrapperSize - (callPatchPos + 4));
        code[callPatchPos]     = static_cast<uint8_t>(callRel & 0xFF);
        code[callPatchPos + 1] = static_cast<uint8_t>((callRel >> 8) & 0xFF);
        code[callPatchPos + 2] = static_cast<uint8_t>((callRel >> 16) & 0xFF);
        code[callPatchPos + 3] = static_cast<uint8_t>((callRel >> 24) & 0xFF);
    }
#endif
    jitEntryOffset_ = wrapperSize;

    x64::encodeMirBuffer(
        mirBuf,
        code,
        debug ? debug->asmOut : nullptr,
        0, // baseOffset=0: enc.here() already includes wrapper bytes
        &vregAlloc,
        debug ? debug->instructionBoundaries : nullptr,
        debugTraceFn,
        faultExitOffset);
    return LowerResult::Ok;
}

JitEntryFn X64Backend::load(std::unique_ptr<CompiledCode> code) {
    if (!code || code->code.empty())
        return nullptr;

    size_t size     = code->code.size();
    size_t pageSize = 4096;
    size_t aligned  = (size + pageSize - 1) & ~(pageSize - 1);

    void *mem = allocExecutable(aligned);
    if (!mem)
        return nullptr;

    allocatedPages_.push_back(mem);
    std::memcpy(mem, code->code.data(), size);

    return reinterpret_cast<JitEntryFn>(mem);
}

void X64Backend::unload(JitEntryFn fn) {
    for (auto it = allocatedPages_.begin(); it != allocatedPages_.end(); ++it) {
        if (reinterpret_cast<void *>(fn) == *it) {
            freeExecutable(*it, 4096);
            allocatedPages_.erase(it);
            break;
        }
    }
}

} // namespace camel::jit
