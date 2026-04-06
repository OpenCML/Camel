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
 * Updated: Apr. 06, 2026
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

#include <cstring>
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

// Camel 标准槽模型：每个 slot 一字（8 字节），布尔/32位/64位/指针均占一槽；JIT
// 生成的所有槽访问必须为 8 字节
static_assert(sizeof(slot_t) == 8, "JIT assumes one word per slot");

// JIT 接收 slot_t*（动态区基址），基址偏移为 0
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
    // 每槽一字（sizeof(slot_t)==8），disp 为字节偏移
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
    using namespace x64;
    const CompilationDebugOptions *debug = unit.debug;

    // Keep failure handling local so every opcode branch can return a precise
    // bytecode PC and reason without threading error strings through helpers.
    auto fail = [&](const std::string &msg) {
        if (failureReason)
            *failureReason = msg;
        return false;
    };

    if (!unit.graph)
        return fail("null graph in JIT compilation unit");
    if (!unit.graph->finalized())
        return fail("graph '" + unit.graph->name() + "' is not sealed");
    if (!unit.graph->hasFrameLayout())
        return fail("incomplete frame layout for graph '" + unit.graph->name() + "'");

    const Bytecode *base = unit.bytecodes.data();
    size_t pcEnd         = unit.bytecodes.size();
    size_t entryPc       = unit.entryPc;

    const slot_t *staticBase = unit.graph->staticArea()->data();
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
    x64::VRegId nextVReg = 0;

    // Compare-Branch fusion state: when a comparison detects a following BRCH
    // using its result, it emits only VLoadFromFrame + VCmpRegImm (no setcc/store),
    // and records info so BRCH can emit a single Jcc.
    struct FusedCmpInfo {
        bool active           = false;
        OpCode cmpOp          = OpCode::RETN;
        data_idx_t resultSlot = 0; // comparison's result slot (= BRCH's condSlot)
    } fusedCmp;

    // BRCH/JOIN simplification: when fused cmp-BRCH is active, the then-path
    // is handled by JUMP (writes w0 directly), so JOIN only executes from the
    // else-path where idx is always 1. We skip the branch-index write entirely
    // and let JOIN take w1 directly.
    struct FusedBrchJoinInfo {
        bool active        = false;
        data_idx_t idxSlot = 0;
    } fusedBrchJoin;

    // Read-only slot cache:
    // if a dynamic slot is never written by any bytecode in this function,
    // repeated loads of that slot can be replaced by VCopy from the first load.
    //
    // This is deliberately stronger than a local peephole because the source
    // slot is immutable for the whole function, so the cache may survive normal
    // control-flow edges. Calls still clear the cache because caller-saved regs
    // are clobbered even if the logical slot value itself is unchanged.
    std::unordered_set<data_idx_t> writtenSlots;
    for (size_t p = entryPc; p < pcEnd; p += base[p].opsize)
        if (base[p].result > 0)
            writtenSlots.insert(base[p].result);
    std::unordered_map<int, x64::VRegId> slotCache;

    auto loadSlot = [&](data_idx_t slotIdx, int disp, x64::VRegId v) {
        // Positive indices address the dynamic frame, negative indices address
        // compile-time-known static slots.
        if (slotIdx > 0) {
            auto it = slotCache.find(disp);
            if (it != slotCache.end()) {
                build.emitVCopy(v, it->second);
                return;
            }
            build.emitVLoadFromFrame(v, disp);
            if (!writtenSlots.count(slotIdx))
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
        build.setNextPc(static_cast<uint32_t>(pc));
        if (debug && debug->enableDebugTrace) {
            build.emitDebugTrace(static_cast<uint32_t>(pc));
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
                // Fused compare-branch: CPU flags already set by preceding cmp.
                // Phase K: skip branch-index write — JOIN only executes from
                // the else-path where idx would always be 1, so we tell JOIN
                // to take w1 directly via fusedBrchJoin.
                fusedBrchJoin = {true, bc.result};
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

            if (fusedBrchJoin.active && fusedBrchJoin.idxSlot == idxSlot) {
                // Phase K: fused cmp-BRCH already handled then-path via JUMP.
                // JOIN only executes from else-path, so take w1 directly.
                x64::VRegId v1 = nextVReg++;
                loadSlot(w1, d1, v1);

                // Phase N: if JOIN is immediately followed by RETN reading
                // JOIN's result, emit VRet directly — skip the intermediate store.
                size_t nextPc = pc + bc.opsize;
                if (nextPc < pcEnd && base[nextPc].opcode == OpCode::RETN &&
                    base[nextPc].fastop[0] == bc.result) {
                    build.emitVRet(v1);
                    // Advance pc so the loop's `pc += bc.opsize` lands past RETN.
                    pc = nextPc + base[nextPc].opsize - bc.opsize;
                } else {
                    build.emitVStoreToFrame(dr, v1);
                }
                fusedBrchJoin.active = false;
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
                build.emitVTest(v2, dIdx);
                build.emitVCmovnz(v3, v1);
                build.emitVStoreToFrame(dr, v3);
            }
            break;
        }
        case OpCode::FUNC: {
            // NativeJitFuncCall 同时支持 Win64 和 SysV AMD64；两者共享帧池管理逻辑，
            // ABI 差异（调用约定、影子空间）均在 mir_encode.cpp 中通过 #if 分支处理。
            if (unit.poolTopAddr) {
                GIR::Graph *targetGraph = getFuncExtraGraph(&bc);
                bool sameGraph          = (targetGraph == unit.graph);
                // NativeJitCallParams is the bridge between MIR lowering and the
                // encoder's call expander. Everything the encoder needs for fast
                // path / slow path selection is packed here once.
                auto *params            = new NativeJitCallParams{};
                params->poolTopAddr     = reinterpret_cast<uint64_t>(unit.poolTopAddr);
                params->targetGraphAddr = reinterpret_cast<uint64_t>(targetGraph);
                params->resultDisp      = slotDisp(bc.result);
                params->argsCnt         = static_cast<uint8_t>(bc.normCnt());
                for (uint8_t ai = 0; ai < params->argsCnt; ++ai)
                    params->argSrcDisps[ai] = slotDisp(bc.operands()[ai]);
                std::memset(params->argVRegs, 0xFF, sizeof(params->argVRegs));
                params->isSameGraph = sameGraph;
                params->extra2Addr  = reinterpret_cast<uint64_t>(bc.extra2());
                params->fastop1Addr = reinterpret_cast<uint64_t>(&bc.fastop[1]);
                params->frameless   = sameGraph;
                if (sameGraph) {
                    // Frameless is only valid for self-recursion today: the
                    // callee layout matches the current graph exactly, so we can
                    // allocate a stack-backed frame and jump straight to the JIT
                    // entry without touching the frame pool.
                    size_t slotCount        = targetGraph->runtimeDataType()->size();
                    size_t rawBytes         = slotCount * sizeof(slot_t);
                    params->calleeSlotBytes = static_cast<uint32_t>((rawBytes + 15u) & ~15u);
                } else {
                    params->calleeSlotBytes = 0;
                }
                if (sameGraph) {
                    params->slowPathFnAddr =
                        reinterpret_cast<uint64_t>(unit.directSelfFuncInvokeAddr);
                    params->slowPathBcAddr = reinterpret_cast<uint64_t>(&bc);
                    params->slowPathPc     = 0;
                } else {
                    params->slowPathFnAddr = reinterpret_cast<uint64_t>(unit.trampolineFunc);
                    params->slowPathBcAddr = 0;
                    params->slowPathPc     = static_cast<uint32_t>(pc);
                }
                // Phase L: for frameless calls, emit visible VLoadFromFrame for
                // each arg so that peephole can fuse preceding store+load pairs.
                if (params->frameless) {
                    uint8_t nArgs = params->argsCnt < 7 ? params->argsCnt : 7;
                    for (uint8_t ai = 0; ai < nArgs; ++ai) {
                        x64::VRegId vArg = nextVReg++;
                        loadSlot(bc.operands()[ai], params->argSrcDisps[ai], vArg);
                        params->argVRegs[ai] = static_cast<uint8_t>(vArg);
                    }
                    // Phase Q: externalize result store so peephole can fuse
                    // the subsequent VLoadFromFrame into a VCopy.
                    x64::VRegId vResult = nextVReg++;
                    params->resultVReg  = static_cast<uint8_t>(vResult);
                }
                build.emitNativeJitFuncCall(params);
                if (params->frameless)
                    build.emitVStoreToFrame(
                        params->resultDisp,
                        static_cast<x64::VRegId>(params->resultVReg));
                // Native calls are the main cache barrier: physical registers
                // may no longer hold the previously cached slot values.
                slotCache.clear();
                break;
            }
            if (!unit.trampolineFunc)
                return fail("pc=" + std::to_string(pc) + " no FUNC trampoline");
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineFunc);
#if defined(_WIN32) || defined(_WIN64)
            build.emitCallTrampolineWin64(static_cast<uint32_t>(pc), addr);
#else
            build.emitCallTrampolineSysV(static_cast<uint32_t>(pc), addr);
#endif
            int dr           = slotDisp(bc.result);
            x64::VRegId vRet = nextVReg++;
            build.emitVMovFromRax(vRet);
            build.emitVStoreToFrame(dr, vRet);
            slotCache.clear();
            break;
        }
        case OpCode::TAIL: {
#if defined(_WIN32) || defined(_WIN64)
            if (getFuncExtraGraph(&bc) == unit.graph) {
                // Self-tail-call is reduced to "rewrite argument slots + jump to
                // entry". No call instruction is emitted, so no new frame is
                // created and recursion stays in the current activation.
                size_t argsCnt         = bc.normCnt();
                const data_idx_t *args = bc.operands();
                std::vector<x64::VRegId> argRegs;
                argRegs.reserve(argsCnt);
                for (size_t i = 0; i < argsCnt; ++i) {
                    x64::VRegId v = nextVReg++;
                    argRegs.push_back(v);
                    loadSlot(args[i], slotDisp(args[i]), v);
                }
                for (size_t i = 0; i < argsCnt; ++i)
                    build.emitVStoreToFrame(slotDisp(static_cast<int>(i + 1)), argRegs[i]);
                build.emitJmpRel32(static_cast<uint32_t>(entryPc));
                break;
            }
            // Cross-graph direct tail-jump used to bypass trampolineTail once the
            // target graph had a compiled entry. That path is currently too
            // fragile for alternating mutual-tail recursion because frame
            // ownership changes across graphs become harder to reason about.
            // Prefer the trampoline path here: it is slightly less direct but
            // preserves correctness and still keeps self-tail recursion on the
            // zero-overhead local fast path above.
#endif
            if (!unit.trampolineTail)
                return fail("pc=" + std::to_string(pc) + " no TAIL trampoline");
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineTail);
#if defined(_WIN32) || defined(_WIN64)
            build.emitCallTrampolineWin64(static_cast<uint32_t>(pc), addr);
#else
            build.emitCallTrampolineSysV(static_cast<uint32_t>(pc), addr);
#endif
            build.emitRet();
            break;
        }
        case OpCode::JUMP: {
            size_t target = static_cast<size_t>(bc.fastop[0]);
            // 若目标是 JOIN 且当前块是 BRCH 的“第一分支”（仅含此 JUMP），直接写回 w0 并跳到 JOIN
            // 之后，避免经 JOIN 读未初始化的 w1
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
            int d0         = slotDisp(bc.fastop[0]);
            x64::VRegId v0 = nextVReg++;
            loadSlot(bc.fastop[0], d0, v0);
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

    // rmir：字节码直接得到的 vreg MIR，未做优化，直接打印并返回
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
        return true;
    }

    // Run MIR cleanup before allocation. These passes still operate on virtual
    // registers, so they can rewrite loads/stores without worrying about
    // physical register side effects.
    x64::runMirOptimizationPasses(mirBuf);

    // mir：优化后的 vreg MIR，打印并返回（不分配、不编码）
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
        return true;
    }

    VRegAllocation vregAlloc;
    linearScanVReg(mirBuf, &vregAlloc);
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
    size_t wrapperSize = 0;
#if defined(_WIN32) || defined(_WIN64)
    {
        uint64_t poolAddr  = unit.poolTopAddr ? reinterpret_cast<uint64_t>(unit.poolTopAddr) : 0;
        uint64_t graphAddr = reinterpret_cast<uint64_t>(unit.graph);
        // push rdi; push rsi; push rbx; push r12  (4 pushes → RSP 8→0→8→0→8 mod 16)
        // 4 pushes gives correct Win64 alignment: body RSP ≡ 0 (mod 16) after wrapper's call.
        code.push_back(0x57);                  // push rdi
        code.push_back(0x56);                  // push rsi
        code.push_back(0x53);                  // push rbx
        code.insert(code.end(), {0x41, 0x54}); // push r12
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
        // call rel32(jitBody)
        code.push_back(0xE8);
        size_t callPatchPos = code.size();
        code.insert(code.end(), {0, 0, 0, 0});
        // pop r12; pop rbx; pop rsi; pop rdi; ret
        code.insert(code.end(), {0x41, 0x5C}); // pop r12
        code.push_back(0x5B);                  // pop rbx
        code.push_back(0x5E);                  // pop rsi
        code.push_back(0x5F);                  // pop rdi
        code.push_back(0xC3);                  // ret
        wrapperSize            = code.size();  // = 42 bytes
        int32_t callRel        = static_cast<int32_t>(wrapperSize - (callPatchPos + 4));
        code[callPatchPos]     = static_cast<uint8_t>(callRel & 0xFF);
        code[callPatchPos + 1] = static_cast<uint8_t>((callRel >> 8) & 0xFF);
        code[callPatchPos + 2] = static_cast<uint8_t>((callRel >> 16) & 0xFF);
        code[callPatchPos + 3] = static_cast<uint8_t>((callRel >> 24) & 0xFF);
    }
#else
    // System V AMD64: slot_t* in rdi, void* ctx in rsi (same as JIT body convention).
    // 使用 3 个 callee-saved push (rbx/r12/r13)，确保调用 JIT body 时 RSP%16==0（满足 SysV
    // 要求，call 指令压栈后 body 入口 RSP%16==8）。r13 作为 jitCtx 的稳定副本，防止
    // trampoline 调用破坏 rsi 后无法传递正确 ctx（rsi 在 SysV 中是 caller-saved）。
    {
        uint64_t poolAddr  = unit.poolTopAddr ? reinterpret_cast<uint64_t>(unit.poolTopAddr) : 0;
        uint64_t graphAddr = reinterpret_cast<uint64_t>(unit.graph);
        // 3 pushes: rbx(callee-saved, holds &FramePool::top_),
        //           r12(callee-saved, holds Graph*),
        //           r13(callee-saved, holds jitCtx copy)
        code.push_back(0x53);                  // push rbx
        code.insert(code.end(), {0x41, 0x54}); // push r12
        code.insert(code.end(), {0x41, 0x55}); // push r13
        // mov r13, rsi  — 将 jitCtx 存入 callee-saved r13，跨 trampoline 调用保持有效
        // REX.W=1,REX.B=1(r13 in rm field), opcode=0x89(MOV r/m64,r64),
        // ModRM=0xF5(mod=11,reg=rsi=6,rm=r13=5)
        code.insert(code.end(), {0x49, 0x89, 0xF5}); // mov r13, rsi
        // mov rbx, imm64(poolTopAddr)  — JIT 内部约定：rbx 缓存 &FramePool::top_
        code.push_back(0x48);
        code.push_back(0xBB);
        for (int b = 0; b < 8; ++b)
            code.push_back(static_cast<uint8_t>((poolAddr >> (b * 8)) & 0xFF));
        // mov r12, imm64(graphAddr)  — JIT 内部约定：r12 缓存当前 Graph*
        code.push_back(0x49);
        code.push_back(0xBC);
        for (int b = 0; b < 8; ++b)
            code.push_back(static_cast<uint8_t>((graphAddr >> (b * 8)) & 0xFF));
        // call rel32(jitBody)
        code.push_back(0xE8);
        size_t callPatchPos = code.size();
        code.insert(code.end(), {0, 0, 0, 0});
        // pop r13; pop r12; pop rbx; ret
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
        debugTraceFn);
    return true;
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
