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
 * Updated: Feb. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "x64_backend.h"

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_builder.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_encode.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_optimize.h"
#include "builtin/passes/sched/linear/fastvm/jit/regalloc/regalloc.h"
#include "builtin/passes/sched/linear/fastvm/jit/runtime/jit_debug_trace.h"
#include "core/context/frame.h"
#include "core/rtdata/data.h"
#include "core/rtdata/tuple.h"
#include "utils/assert.h"
#include "utils/log.h"

#include <cstring>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace camel::jit {

namespace {

// 用 VReg 加载一个操作数到 v（frame 或 static mem）
inline void loadOperandToVReg(
    const Bytecode &bc, int opIdx, int disp, uint64_t staticAddr, x64::VRegId v,
    x64::MirBuilder &build) {
    if (bc.fastop[opIdx] > 0)
        build.emitVLoadFromFrame(v, disp);
    else
        build.emitVLoadFromMemAt(v, staticAddr);
}

// 用 VReg 写回结果（始终写 frame disp）
inline void storeResultFromVReg(int disp, x64::VRegId v, x64::MirBuilder &build) {
    build.emitVStoreToFrame(disp, v);
}

// Float：槽低 4 字节为 float，用 VXmm32* 加载/写回
inline void loadFloatOperandToVReg(
    const Bytecode &bc, int opIdx, int disp, uint64_t staticAddr, x64::VRegId v,
    x64::MirBuilder &build) {
    if (bc.fastop[opIdx] > 0)
        build.emitVXmm32LoadFromFrame(v, disp);
    else
        build.emitVXmm32LoadFromMemAt(v, staticAddr);
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
    result->entryOffset = 0;
    return result;
}

bool X64Backend::compileBytecode(
    const CompilationUnit &unit, std::vector<uint8_t> &code, std::string *failureReason) {
    using namespace x64;

    auto fail = [&](const std::string &msg) {
        if (failureReason)
            *failureReason = msg;
        return false;
    };

    if (!unit.frameMeta)
        return fail("no FrameMeta for graph '" + unit.graph->name() + "'");

    const Bytecode *base = unit.bytecodes.data();
    size_t pcEnd         = unit.bytecodes.size();
    size_t entryPc       = unit.entryPc;

    // 寄存器分配：若 mirSlotOnly 则全部溢出，便于可读 MIR dump；否则线性扫描
    AllocationResult alloc = unit.mirSlotOnly ? makeAllSpilledAlloc(unit.bytecodes, entryPc, pcEnd)
                                              : linearScanAllocate(unit.bytecodes, entryPc, pcEnd);

    EXEC_WHEN_DEBUG(([&]() {
        int inReg = 0, spilled = 0;
        for (size_t i = 1; i < alloc.slotToReg.size(); ++i) {
            if (alloc.slotToReg[i] >= 0)
                ++inReg;
            else
                ++spilled;
        }
        l.in("JIT.Backend")
            .debug(
                "RegAlloc for graph '{}': {} slots in reg, {} spilled (rax/rcx/rdx/rbx/r8-r11)",
                unit.graph->name(),
                inReg,
                spilled);
    }()));

    const slot_t *staticBase = unit.frameMeta->staticArea->data();
    auto staticSlotAddr      = [&](data_idx_t idx) -> uint64_t {
        return reinterpret_cast<uint64_t>(staticBase + static_cast<size_t>(-idx));
    };
    auto staticSlotAddrWithComment = [&](data_idx_t idx) -> uint64_t {
        uint64_t a = staticSlotAddr(idx);
        if (unit.mirOut && unit.mirSymbolNames)
            (*unit.mirSymbolNames)[a] = "static slot[" + std::to_string(idx) + "]";
        return a;
    };

    // 构建 MIR → 优化 → vreg 分配 → 根据 MIR 计算 pcToOffset → 编码（L2 架构）
    x64::MirBuffer mirBuf;
    x64::MirBuilder build(mirBuf);
    x64::VRegId nextVReg = 0; // 虚拟寄存器计数器，供 JOIN 等使用
#if defined(_WIN32) || defined(_WIN64)
    build.emitPrologueWin64();
#endif

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];
        build.setNextPc(static_cast<uint32_t>(pc));
#ifndef NDEBUG
        build.emitDebugTrace(static_cast<uint32_t>(
            pc)); // 插入到该 pc 对应 MIR 之前，jmp/ret 后仍会执行到目标 pc 的 trace
#endif
        switch (bc.opcode) {
        case OpCode::LADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVAdd(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LSUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVSub(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmAdd(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DSUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmSub(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmMul(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmDiv(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DLT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmCmpSetB(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmCmpSetA(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmCmpSetE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DNE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmCmpSetNZ(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DLE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmCmpSetBE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::DGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmmCmpSetAE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVAdd32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::ISUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVSub32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVMul32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVIdiv32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::ILT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetL32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetG32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::INE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetNE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::ILE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetLE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::IGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetGE32(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FADD: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32Add(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FSUB: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32Sub(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32Mul(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32Div(vr, v0, v1);
            storeFloatResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FLT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32CmpSetB(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32CmpSetA(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32CmpSetE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FNE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32CmpSetNZ(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FLE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32CmpSetBE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::FGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++, v1 = nextVReg++, vr = nextVReg++;
            loadFloatOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadFloatOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVXmm32CmpSetAE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LMUL: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVMul(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LDIV: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVIdiv(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LLE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetLE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LLT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetL(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LGT: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetG(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LEQ: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LNE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetNE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::LGE: {
            int d0 = slotDisp(bc.fastop[0]), d1 = slotDisp(bc.fastop[1]), dr = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId vr = nextVReg++;
            loadOperandToVReg(bc, 0, d0, staticSlotAddrWithComment(bc.fastop[0]), v0, build);
            loadOperandToVReg(bc, 1, d1, staticSlotAddrWithComment(bc.fastop[1]), v1, build);
            build.emitVCmpSetGE(vr, v0, v1);
            storeResultFromVReg(dr, vr, build);
            break;
        }
        case OpCode::BRCH: {
            if (bc.withCnt() != 0)
                return fail(
                    "pc=" + std::to_string(pc) +
                    " BRCH only supports simple if-else (withCnt=0); match-case (withCnt>0) not "
                    "yet implemented, got " +
                    std::to_string(bc.withCnt()));
            data_idx_t condIdx = bc.nargs()[0];
            if (condIdx <= 0)
                return fail(
                    "pc=" + std::to_string(pc) + " BRCH cond slot index must be positive, got " +
                    std::to_string(condIdx));
            int dc            = slotDisp(condIdx);
            int dr            = slotDisp(bc.result);
            size_t t0         = pc + bc.opsize;
            size_t t1         = pc + bc.opsize + 1;
            x64::VRegId vCond = nextVReg++;
            x64::VRegId vZero = nextVReg++;
            x64::VRegId vOne  = nextVReg++;
            x64::VRegId vRes  = nextVReg++;
            if (condIdx > 0)
                build.emitVLoadFromFrame(vCond, dc);
            else
                build.emitVLoadFromMemAt(vCond, staticSlotAddrWithComment(condIdx));
            build.emitVLoadImm32(vZero, 0);
            build.emitVLoadImm32(vOne, 1);
            build.emitVCopy(vRes, vZero);
            build.emitVTest(vCond);
            build.emitVCmovnz(vRes, vOne);
            build.emitVStoreToFrame(dr, vRes);
            build.emitJzRel32(static_cast<uint32_t>(t1));
            build.emitJmpRel32(static_cast<uint32_t>(t0));
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
            int dIdx = slotDisp(idxSlot), d0 = slotDisp(w0), d1 = slotDisp(w1),
                dr         = slotDisp(bc.result);
            x64::VRegId v0 = nextVReg++;
            x64::VRegId v1 = nextVReg++;
            x64::VRegId v2 = nextVReg++;
            x64::VRegId v3 = nextVReg++;
            // result = idx ? w1 : w0 → v3 = (v2 ? v1 : v0)：先 v3=v0，test v2，cmovnz v3,v1
            if (w0 > 0)
                build.emitVLoadFromFrame(v0, d0);
            else
                build.emitVLoadFromMemAt(v0, staticSlotAddrWithComment(w0));
            if (w1 > 0)
                build.emitVLoadFromFrame(v1, d1);
            else
                build.emitVLoadFromMemAt(v1, staticSlotAddrWithComment(w1));
            if (idxSlot > 0)
                build.emitVLoadFromFrame(v2, dIdx);
            else
                build.emitVLoadFromMemAt(v2, staticSlotAddrWithComment(idxSlot));
            build.emitVCopy(v3, v0); // v3 = w0 (default)
            build.emitVTest(v2);
            build.emitVCmove(
                v3,
                v1); // if v2==0 (n>1) then v3 = v1（递归结果）；n≤1 已走第一分支直接返回
            build.emitVStoreToFrame(dr, v3);
            break;
        }
        case OpCode::FUNC: {
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
            break;
        }
        case OpCode::TAIL: {
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
                    int d0         = slotDisp(joinBc.wargs()[0]);
                    int dr         = slotDisp(joinBc.result);
                    x64::VRegId v0 = nextVReg++;
                    if (joinBc.wargs()[0] > 0)
                        build.emitVLoadFromFrame(v0, d0);
                    else
                        build.emitVLoadFromMemAt(v0, staticSlotAddrWithComment(joinBc.wargs()[0]));
                    build.emitVStoreToFrame(dr, v0);
                    build.emitJmpRel32(static_cast<uint32_t>(target + joinBc.opsize));
                    break;
                }
            }
            build.emitJmpRel32(static_cast<uint32_t>(target));
            break;
        }
        case OpCode::RETN: {
            int d0         = slotDisp(bc.fastop[0]);
            x64::VRegId v0 = nextVReg++;
            if (bc.fastop[0] > 0)
                build.emitVLoadFromFrame(v0, d0);
            else
                build.emitVLoadFromMemAt(v0, staticSlotAddrWithComment(bc.fastop[0]));
            build.emitVRet(v0);
            break;
        }
        case OpCode::OPER: {
            if (!unit.trampolineOper)
                return fail("pc=" + std::to_string(pc) + " no OPER trampoline");
            uint64_t addr     = reinterpret_cast<uint64_t>(unit.trampolineOper);
            uint64_t graphPtr = reinterpret_cast<uint64_t>(unit.graph);
#if defined(_WIN32) || defined(_WIN64)
            build.emitCallTrampolineOperWin64(static_cast<uint32_t>(pc), graphPtr, addr);
#else
            build.emitCallTrampolineOperSysV(static_cast<uint32_t>(pc), graphPtr, addr);
#endif
            x64::VRegId vRet = nextVReg++;
            build.emitVMovFromRax(vRet);
            build.emitVStoreToFrame(slotDisp(bc.result), vRet);
            break;
        }
        default:
            return fail("pc=" + std::to_string(pc) + " unsupported opcode " + to_string(bc.opcode));
        }

        pc += bc.opsize;
    }

    // rmir：字节码直接得到的 vreg MIR，未做优化，直接打印并返回
    if (unit.mirOut && unit.mirSlotOnly) {
        std::unordered_map<size_t, size_t> pcToOffset;
        size_t offset = 0;
        for (const auto &mi : mirBuf) {
            if (mi.hasPc())
                pcToOffset[static_cast<size_t>(mi.pc)] = offset;
            offset += x64::mirSizeBytes(mi);
        }
        x64::MirPrintOptions opts;
        opts.pcToOffset  = &pcToOffset;
        opts.symbolNames = unit.mirSymbolNames;
        opts.slotNames   = unit.mirSlotNames;
        opts.vregAlloc   = nullptr;
        x64::mirPrint(mirBuf, *unit.mirOut, opts);
        return true;
    }

    // 多遍优化（当前仅 Win64 冗余 mov 等，后续可扩展 CSE、死代码删除等）
    x64::runMirOptimizationPasses(mirBuf);

    // mir：优化后的 vreg MIR，打印并返回（不分配、不编码）
    if (unit.mirOut) {
        std::unordered_map<size_t, size_t> pcToOffset;
        size_t offset = 0;
        for (const auto &mi : mirBuf) {
            if (mi.hasPc())
                pcToOffset[static_cast<size_t>(mi.pc)] = offset;
            offset += x64::mirSizeBytes(mi);
        }
        x64::MirPrintOptions opts;
        opts.pcToOffset  = &pcToOffset;
        opts.symbolNames = unit.mirSymbolNames;
        opts.slotNames   = unit.mirSlotNames;
        opts.vregAlloc   = nullptr;
        x64::mirPrint(mirBuf, *unit.mirOut, opts);
        return true;
    }

    // asm：优化后 MIR → 寄存器分配 + 指令派发 → 汇编/机器码（跳转目标在 encodeMirBuffer 内由 MIR
    // 下标解析）
    VRegAllocation vregAlloc;
    linearScanVReg(mirBuf, &vregAlloc);
    void *debugTraceFn = nullptr;
#ifndef NDEBUG
    debugTraceFn = reinterpret_cast<void *>(&jitDebugTraceWrapper);
#endif
    x64::encodeMirBuffer(
        mirBuf,
        code,
        unit.asmOut,
        0,
        &vregAlloc,
        unit.instructionBoundaries,
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
