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

#include "x64_backend.h"

#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_builder.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_encode.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir_optimize.h"
#include "builtin/passes/sched/linear/fastvm/jit/regalloc/regalloc.h"
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

// 二元 slot 运算：左操作数 loadL，右操作数 opR，结果 storeR（统一用 fastop[0]/[1]/result）
template <typename SlotDispFn, typename StaticAddrFn, typename LoadL, typename OpR, typename StoreR>
void emitBinarySlot(
    const Bytecode &bc, const AllocationResult &alloc, x64::MirBuilder &build,
    SlotDispFn slotDispFn, StaticAddrFn staticAddrFn, LoadL loadL, OpR opR, StoreR storeR) {
    int d0 = slotDispFn(bc.fastop[0]), d1 = slotDispFn(bc.fastop[1]), dr = slotDispFn(bc.result);
    int r0 = alloc.regForSlot(bc.fastop[0]), r1 = alloc.regForSlot(bc.fastop[1]),
        rr = alloc.regForSlot(bc.result);
    loadL(build, r0, d0, bc.fastop[0]);
    opR(build, r1, d1, bc.fastop[1]);
    storeR(build, rr, dr);
}

// 比较：左操作数 loadL，右操作数 cmpR（结果 0/1 在 rax），再写回 result
template <typename SlotDispFn, typename StaticAddrFn, typename LoadL, typename CmpR>
void emitCompareSlot(
    const Bytecode &bc, const AllocationResult &alloc, x64::MirBuilder &build,
    SlotDispFn slotDispFn, StaticAddrFn staticAddrFn, LoadL loadL, CmpR cmpR) {
    int d0 = slotDispFn(bc.fastop[0]), d1 = slotDispFn(bc.fastop[1]), dr = slotDispFn(bc.result);
    int r0 = alloc.regForSlot(bc.fastop[0]), r1 = alloc.regForSlot(bc.fastop[1]),
        rr = alloc.regForSlot(bc.result);
    loadL(build, r0, d0, bc.fastop[0]);
    cmpR(build, r1, d1, bc.fastop[1]);
    if (rr >= 0)
        build.emitMovRegFromRax(static_cast<uint8_t>(rr));
    else
        build.emitMovFrameFromReg(dr, x64::kRegRax);
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

    // 构建 MIR → 优化 → 根据 MIR 计算 pcToOffset → 编码（L2 架构）
    x64::MirBuffer mirBuf;
    x64::MirBuilder build(mirBuf);
#if defined(_WIN32) || defined(_WIN64)
    build.emitPrologueWin64();
#endif

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];
        build.setNextPc(static_cast<uint32_t>(pc));

        switch (bc.opcode) {
        case OpCode::LADD: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitAddRaxFromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitAddRaxFromFrame(d1);
            else
                build.emitAddRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LSUB: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitSubRaxFromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitSubRaxFromFrame(d1);
            else
                build.emitSubRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::DADD: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitAddXmm0FromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitAddXmm0FromFrame(d1);
            else
                build.emitAddXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromXmm0(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromXmm0(dr);
            break;
        }
        case OpCode::DSUB: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitSubXmm0FromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitSubXmm0FromFrame(d1);
            else
                build.emitSubXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromXmm0(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromXmm0(dr);
            break;
        }
        case OpCode::DMUL: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitMulXmm0FromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitMulXmm0FromFrame(d1);
            else
                build.emitMulXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromXmm0(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromXmm0(dr);
            break;
        }
        case OpCode::DDIV: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitDivXmm0FromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitDivXmm0FromFrame(d1);
            else
                build.emitDivXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromXmm0(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromXmm0(dr);
            break;
        }
        case OpCode::DLT: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitComisdXmm0RegSetb(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitComisdXmm0FrameSetb(d1);
            else
                build.emitComisdXmm0MemAtSetb(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::DGT: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitComisdXmm0RegSeta(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitComisdXmm0FrameSeta(d1);
            else
                build.emitComisdXmm0MemAtSeta(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::DEQ: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitComisdXmm0RegSete(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitComisdXmm0FrameSete(d1);
            else
                build.emitComisdXmm0MemAtSete(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::DNE: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitComisdXmm0RegSetnz(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitComisdXmm0FrameSetnz(d1);
            else
                build.emitComisdXmm0MemAtSetnz(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::DLE: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitComisdXmm0RegSetbe(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitComisdXmm0FrameSetbe(d1);
            else
                build.emitComisdXmm0MemAtSetbe(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::DGE: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovXmm0FromFrame(d0);
            else
                build.emitMovXmm0FromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitComisdXmm0RegSetae(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitComisdXmm0FrameSetae(d1);
            else
                build.emitComisdXmm0MemAtSetae(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
#define EMIT_BINARY_EAX(loadRight)                                                                 \
    emitBinarySlot(                                                                                \
        bc,                                                                                        \
        alloc,                                                                                     \
        build,                                                                                     \
        [this](int i) { return slotDisp(i); },                                                     \
        staticSlotAddr,                                                                            \
        [&](x64::MirBuilder &b, int r, int d, int op) {                                            \
            if (r >= 0)                                                                            \
                b.emitMovEaxFromReg(static_cast<uint8_t>(r));                                      \
            else if (op > 0)                                                                       \
                b.emitMovEaxFromFrame(d);                                                          \
            else                                                                                   \
                b.emitMovEaxFromMemAt(staticSlotAddrWithComment(op));                              \
        },                                                                                         \
        loadRight,                                                                                 \
        [&](x64::MirBuilder &b, int rr, int dr) {                                                  \
            if (rr >= 0)                                                                           \
                b.emitMovRegFromRax(static_cast<uint8_t>(rr));                                     \
            else                                                                                   \
                b.emitMovFrameFromReg(dr, x64::kRegRax);                                           \
        })
        case OpCode::IADD:
            EMIT_BINARY_EAX([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitAddEaxFromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitAddEaxFromFrame(d);
                else
                    b.emitAddEaxFromMemAt(staticSlotAddrWithComment(op));
            });
            break;
        case OpCode::ISUB:
            EMIT_BINARY_EAX([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitSubEaxFromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitSubEaxFromFrame(d);
                else
                    b.emitSubEaxFromMemAt(staticSlotAddrWithComment(op));
            });
            break;
        case OpCode::IMUL:
            EMIT_BINARY_EAX([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitMulEaxFromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitMulEaxFromFrame(d);
                else
                    b.emitMulEaxFromMemAt(staticSlotAddrWithComment(op));
            });
            break;
        case OpCode::IDIV:
            EMIT_BINARY_EAX([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitIdivEaxByReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitIdivEaxByFrame(d);
                else
                    b.emitIdivEaxByMemAt(staticSlotAddrWithComment(op));
            });
            break;
#undef EMIT_BINARY_EAX
#define EMIT_CMP_EAX(emitCmpFrame, emitCmpMemAt, emitCmpReg)                                       \
    emitCompareSlot(                                                                               \
        bc,                                                                                        \
        alloc,                                                                                     \
        build,                                                                                     \
        [this](int i) { return slotDisp(i); },                                                     \
        staticSlotAddr,                                                                            \
        [&](x64::MirBuilder &b, int r, int d, int op) {                                            \
            if (r >= 0)                                                                            \
                b.emitMovEaxFromReg(static_cast<uint8_t>(r));                                      \
            else if (op > 0)                                                                       \
                b.emitMovEaxFromFrame(d);                                                          \
            else                                                                                   \
                b.emitMovEaxFromMemAt(staticSlotAddrWithComment(op));                              \
        },                                                                                         \
        [&](x64::MirBuilder &b, int r, int d, int op) {                                            \
            if (r >= 0)                                                                            \
                b.emitCmpReg(static_cast<uint8_t>(r));                                             \
            else if (op > 0)                                                                       \
                b.emitCmpFrame(d);                                                                 \
            else                                                                                   \
                b.emitCmpMemAt(staticSlotAddrWithComment(op));                                     \
        })
        case OpCode::ILT:
            EMIT_CMP_EAX(emitCmpEaxFrameSetl, emitCmpEaxMemAtSetl, emitCmpEaxRegSetl);
            break;
        case OpCode::IGT:
            EMIT_CMP_EAX(emitCmpEaxFrameSetg, emitCmpEaxMemAtSetg, emitCmpEaxRegSetg);
            break;
        case OpCode::IEQ:
            EMIT_CMP_EAX(emitCmpEaxFrameSete, emitCmpEaxMemAtSete, emitCmpEaxRegSete);
            break;
        case OpCode::INE:
            EMIT_CMP_EAX(emitCmpEaxFrameSetne, emitCmpEaxMemAtSetne, emitCmpEaxRegSetne);
            break;
        case OpCode::ILE:
            EMIT_CMP_EAX(emitCmpEaxFrameSetle, emitCmpEaxMemAtSetle, emitCmpEaxRegSetle);
            break;
        case OpCode::IGE:
            EMIT_CMP_EAX(emitCmpEaxFrameSetge, emitCmpEaxMemAtSetge, emitCmpEaxRegSetge);
            break;
#undef EMIT_CMP_EAX
#define EMIT_BINARY_SS(loadRight)                                                                  \
    emitBinarySlot(                                                                                \
        bc,                                                                                        \
        alloc,                                                                                     \
        build,                                                                                     \
        [this](int i) { return slotDisp(i); },                                                     \
        staticSlotAddr,                                                                            \
        [&](x64::MirBuilder &b, int r, int d, int op) {                                            \
            if (r >= 0)                                                                            \
                b.emitMovSsXmm0FromReg(static_cast<uint8_t>(r));                                   \
            else if (op > 0)                                                                       \
                b.emitMovSsXmm0FromFrame(d);                                                       \
            else                                                                                   \
                b.emitMovSsXmm0FromMemAt(staticSlotAddrWithComment(op));                           \
        },                                                                                         \
        loadRight,                                                                                 \
        [&](x64::MirBuilder &b, int rr, int dr) {                                                  \
            if (rr >= 0)                                                                           \
                b.emitMovSsRegFromXmm0(static_cast<uint8_t>(rr));                                  \
            else                                                                                   \
                b.emitMovSsFrameFromXmm0(dr);                                                      \
        })
        case OpCode::FADD:
            EMIT_BINARY_SS([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitAddSsXmm0FromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitAddSsXmm0FromFrame(d);
                else
                    b.emitAddSsXmm0FromMemAt(staticSlotAddrWithComment(op));
            });
            break;
        case OpCode::FSUB:
            EMIT_BINARY_SS([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitSubSsXmm0FromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitSubSsXmm0FromFrame(d);
                else
                    b.emitSubSsXmm0FromMemAt(staticSlotAddrWithComment(op));
            });
            break;
        case OpCode::FMUL:
            EMIT_BINARY_SS([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitMulSsXmm0FromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitMulSsXmm0FromFrame(d);
                else
                    b.emitMulSsXmm0FromMemAt(staticSlotAddrWithComment(op));
            });
            break;
        case OpCode::FDIV:
            EMIT_BINARY_SS([&](x64::MirBuilder &b, int r, int d, int op) {
                if (r >= 0)
                    b.emitDivSsXmm0FromReg(static_cast<uint8_t>(r));
                else if (op > 0)
                    b.emitDivSsXmm0FromFrame(d);
                else
                    b.emitDivSsXmm0FromMemAt(staticSlotAddrWithComment(op));
            });
            break;
#undef EMIT_BINARY_SS
#define EMIT_CMP_SS(emitCmpFrame, emitCmpMemAt, emitCmpReg)                                        \
    emitCompareSlot(                                                                               \
        bc,                                                                                        \
        alloc,                                                                                     \
        build,                                                                                     \
        [this](int i) { return slotDisp(i); },                                                     \
        staticSlotAddr,                                                                            \
        [&](x64::MirBuilder &b, int r, int d, int op) {                                            \
            if (r >= 0)                                                                            \
                b.emitMovSsXmm0FromReg(static_cast<uint8_t>(r));                                   \
            else if (op > 0)                                                                       \
                b.emitMovSsXmm0FromFrame(d);                                                       \
            else                                                                                   \
                b.emitMovSsXmm0FromMemAt(staticSlotAddrWithComment(op));                           \
        },                                                                                         \
        [&](x64::MirBuilder &b, int r, int d, int op) {                                            \
            if (r >= 0)                                                                            \
                b.emitCmpReg(static_cast<uint8_t>(r));                                             \
            else if (op > 0)                                                                       \
                b.emitCmpFrame(d);                                                                 \
            else                                                                                   \
                b.emitCmpMemAt(staticSlotAddrWithComment(op));                                     \
        })
        case OpCode::FLT:
            EMIT_CMP_SS(emitComissXmm0FrameSetb, emitComissXmm0MemAtSetb, emitComissXmm0RegSetb);
            break;
        case OpCode::FGT:
            EMIT_CMP_SS(emitComissXmm0FrameSeta, emitComissXmm0MemAtSeta, emitComissXmm0RegSeta);
            break;
        case OpCode::FEQ:
            EMIT_CMP_SS(emitComissXmm0FrameSete, emitComissXmm0MemAtSete, emitComissXmm0RegSete);
            break;
        case OpCode::FNE:
            EMIT_CMP_SS(emitComissXmm0FrameSetnz, emitComissXmm0MemAtSetnz, emitComissXmm0RegSetnz);
            break;
        case OpCode::FLE:
            EMIT_CMP_SS(emitComissXmm0FrameSetbe, emitComissXmm0MemAtSetbe, emitComissXmm0RegSetbe);
            break;
        case OpCode::FGE:
            EMIT_CMP_SS(emitComissXmm0FrameSetae, emitComissXmm0MemAtSetae, emitComissXmm0RegSetae);
            break;
#undef EMIT_CMP_SS
        case OpCode::LLE: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            if (bc.fastop[1] > 0)
                build.emitCmpRaxFrameSetle(d1);
            else
                build.emitCmpRaxMemAtSetle(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LLT: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            if (bc.fastop[1] > 0)
                build.emitCmpRaxFrameSetl(d1);
            else
                build.emitCmpRaxMemAtSetl(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LGT: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            if (bc.fastop[1] > 0)
                build.emitCmpRaxFrameSetg(d1);
            else
                build.emitCmpRaxMemAtSetg(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LEQ: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            if (bc.fastop[1] > 0)
                build.emitCmpRaxFrameSete(d1);
            else
                build.emitCmpRaxMemAtSete(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LNE: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            if (bc.fastop[1] > 0)
                build.emitCmpRaxFrameSetne(d1);
            else
                build.emitCmpRaxMemAtSetne(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LGE: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            if (bc.fastop[1] > 0)
                build.emitCmpRaxFrameSetge(d1);
            else
                build.emitCmpRaxMemAtSetge(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LMUL: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitMulRaxFromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitMulRaxFromFrame(d1);
            else
                build.emitMulRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        case OpCode::LDIV: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(bc.fastop[0]));
            if (r1 >= 0)
                build.emitIdivRaxByReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                build.emitIdivRaxByFrame(d1);
            else
                build.emitIdivRaxByMemAt(staticSlotAddrWithComment(bc.fastop[1]));
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
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
            int dc    = slotDisp(condIdx);
            int dr    = slotDisp(bc.result);
            int rc    = alloc.regForSlot(condIdx);
            size_t t0 = pc + bc.opsize;     // true branch (jumpIdx=0)
            size_t t1 = pc + bc.opsize + 1; // false branch (jumpIdx=1)
            // 用 cmov 只写一次 result：r8=0, r9=1，test 后 cmove r8,r9（ZF 则 r8=1），再 store
            build.emitMovRegImm32(x64::kRegR8, 0);
            build.emitMovRegImm32(x64::kRegR9, 1);
            if (rc > 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(rc));
            else if (rc < 0)
                build.emitMovRegFromFrame(x64::kRegRax, dc);
            build.emitTestRaxRax();
            build.emitCmoveR8FromR9();
            build.emitMovFrameFromReg(dr, x64::kRegR8);
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
                dr   = slotDisp(bc.result);
            int rIdx = alloc.regForSlot(idxSlot);
            int r0   = alloc.regForSlot(w0);
            int r1   = alloc.regForSlot(w1);
            int rr   = alloc.regForSlot(bc.result);
            if (r0 != 0 && r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else if (w0 > 0)
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(w0));
            build.emitMovRegFromRax(3);
            if (r1 != 0 && r1 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r1));
            else if (w1 > 0)
                build.emitMovRegFromFrame(x64::kRegRax, d1);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(w1));
            build.emitMovRegFromRax(1);
            if (rIdx >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(rIdx));
            else if (idxSlot > 0)
                build.emitMovRegFromFrame(x64::kRegRax, dIdx);
            else
                build.emitMovRaxFromMemAt(staticSlotAddrWithComment(idxSlot));
            build.emitTestRaxRax();
            build.emitCmoveRcxFromRbx();
            build.emitMovRaxFromReg(1);
            if (rr >= 0)
                build.emitMovRegFromRax(static_cast<uint8_t>(rr));
            else
                build.emitMovFrameFromReg(dr, x64::kRegRax);
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
            int dr = slotDisp(bc.result);
            build.emitMovFrameFromReg(dr, x64::kRegRax);
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
            build.emitJmpRel32(static_cast<uint32_t>(target));
            break;
        }
        case OpCode::RETN: {
            int d0 = slotDisp(bc.fastop[0]);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            if (r0 >= 0)
                build.emitMovRaxFromReg(static_cast<uint8_t>(r0));
            else
                build.emitMovRegFromFrame(x64::kRegRax, d0);
            build.emitRet();
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
            int dr = slotDisp(bc.result);
            build.emitMovFrameFromReg(dr, x64::kRegRax);
            break;
        }
        default:
            return fail("pc=" + std::to_string(pc) + " unsupported opcode " + to_string(bc.opcode));
        }

        pc += bc.opsize;
    }

    x64::optimizeMirBuffer(mirBuf);

    std::unordered_map<size_t, size_t> pcToOffset;
    size_t offset = 0;
    for (const auto &mi : mirBuf) {
        if (mi.hasPc())
            pcToOffset[static_cast<size_t>(mi.pc)] = offset;
        offset += x64::mirSizeBytes(mi);
    }

    if (unit.mirOut) {
        x64::MirPrintOptions opts;
        opts.pcToOffset  = &pcToOffset;
        opts.symbolNames = unit.mirSymbolNames;
        opts.slotNames   = unit.mirSlotNames;
        x64::mirPrint(mirBuf, *unit.mirOut, opts);
        return true; // 仅打印 MIR，不编码
    }
    // asm 遍只输出编码后的汇编，不输出 MIR
    x64::encodeMirBuffer(mirBuf, pcToOffset, code, unit.asmOut, 0);
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
