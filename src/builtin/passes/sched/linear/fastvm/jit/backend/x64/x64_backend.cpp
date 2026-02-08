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
 * Updated: Feb. 08, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "x64_backend.h"

#include "builtin/passes/sched/linear/fastvm/jit/regalloc/regalloc.h"
#include "core/context/frame.h"
#include "core/rtdata/data.h"
#include "core/rtdata/tuple.h"
#include "utils/assert.h"
#include "utils/log.h"

#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace camel::jit {

namespace {

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

    const Bytecode *base    = unit.bytecodes.data();
    size_t pcEnd            = unit.bytecodes.size();
    size_t entryPc          = unit.entryPc;
    size_t dynamicSlotCount = unit.frameMeta->runtimeDataType->size();

    // 寄存器分配：线性扫描将 slot 映射到物理寄存器
    AllocationResult alloc = linearScanAllocate(unit.bytecodes, entryPc, pcEnd);

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

    auto slotInRange = [&](data_idx_t idx) {
        return idx <= 0 || static_cast<size_t>(idx) < dynamicSlotCount;
    };
    (void)slotInRange; // 仅 Debug 下 ASSERT 使用，Release 无引用
    auto frameAccessSize = [&](int idx) {
        int d = slotDisp(idx);
        return x64::Encoder::fitsDisp8(d) ? 4 : 7;
    };
    // 静态区 slot 用 mov rbx,imm64 + op [rbx] = 10+3 字节
    constexpr size_t kStaticSlotAccessSize = 13;
    auto operandLoadSize                   = [&](data_idx_t idx, int reg) -> size_t {
        if (reg >= 0)
            return 3;
        if (idx > 0)
            return static_cast<size_t>(frameAccessSize(idx));
        return kStaticSlotAccessSize;
    };
    // DADD/DSUB 用 xmm0，指令长度与 rax 不同
    auto frameAccessSizeXmm = [&](int idx) {
        return x64::Encoder::fitsDisp8(slotDisp(idx)) ? 5 : 9;
    };
    auto operandLoadSizeXmm = [&](data_idx_t idx, int reg) -> size_t {
        if (reg >= 0)
            return 5;
        if (idx > 0)
            return static_cast<size_t>(frameAccessSizeXmm(idx));
        return 14; // movXmm0FromMemAt
    };
    auto operandAddSubSizeXmm = [&](data_idx_t idx, int reg) -> size_t {
        if (reg >= 0)
            return 9; // movq xmm1,r + addsd/subsd
        if (idx > 0)
            return static_cast<size_t>(frameAccessSizeXmm(idx));
        return 14;
    };
    auto resultStoreSizeXmm = [&](int reg, data_idx_t resultIdx) -> size_t {
        if (reg >= 0)
            return 5;
        return static_cast<size_t>(frameAccessSizeXmm(resultIdx));
    };

    // 第一遍：计算每条字节码对应的机器码偏移
    std::unordered_map<size_t, size_t> pcToOffset;
#if defined(_WIN32) || defined(_WIN64)
    constexpr size_t kPrologueSize = 10; // mov rdi,rcx; mov rsi,rdx
#else
    constexpr size_t kPrologueSize = 0;
#endif
    size_t offset = kPrologueSize;
    for (size_t pc = entryPc; pc < pcEnd;) {
        pcToOffset[pc]     = offset;
        const Bytecode &bc = base[pc];
        switch (bc.opcode) {
        case OpCode::LADD:
        case OpCode::LSUB: {
            // ASSERT(
            //     slotInRange(bc.fastop[0]) && slotInRange(bc.fastop[1]) && slotInRange(bc.result),
            //     "LADD/LSUB slot index out of range");
            int r0 = alloc.regForSlot(bc.fastop[0]), r1 = alloc.regForSlot(bc.fastop[1]),
                rr = alloc.regForSlot(bc.result);
            offset += operandLoadSize(bc.fastop[0], r0) + operandLoadSize(bc.fastop[1], r1) +
                      (rr >= 0 ? 3 : frameAccessSize(bc.result));
            break;
        }
        case OpCode::DADD:
        case OpCode::DSUB: {
            // ASSERT(
            //     slotInRange(bc.fastop[0]) && slotInRange(bc.fastop[1]) && slotInRange(bc.result),
            //     "DADD/DSUB slot index out of range");
            int r0 = alloc.regForSlot(bc.fastop[0]), r1 = alloc.regForSlot(bc.fastop[1]),
                rr = alloc.regForSlot(bc.result);
            offset += operandLoadSizeXmm(bc.fastop[0], r0) +
                      operandAddSubSizeXmm(bc.fastop[1], r1) + resultStoreSizeXmm(rr, bc.result);
            break;
        }
        case OpCode::LLE: {
            if (bc.fastop[1] != -1)
                return fail(
                    "pc=" + std::to_string(pc) +
                    " LLE only supports rhs=1 (const), got fastop[1]=" +
                    std::to_string(bc.fastop[1]));
            // ASSERT(
            //     slotInRange(bc.fastop[0]) && slotInRange(bc.result),
            //     "LLE slot index out of range");
            int r0 = alloc.regForSlot(bc.fastop[0]), rr = alloc.regForSlot(bc.result);
            offset += (r0 >= 0 ? 3 : frameAccessSize(bc.fastop[0])) + 5 + 4 +
                      (rr >= 0 ? 3 : frameAccessSize(bc.result)); // mov + cmp/setle/movzx + mov
            break;
        }
        // BRCH: withCnt()==0 为简单 if-else（cond 为 bool，跳 0/1）；withCnt()>0 为 match-case（与
        // wargs[0..N-1] 比较，跳 0..N）
        case OpCode::BRCH: {
            if (bc.withCnt() != 0)
                return fail(
                    "pc=" + std::to_string(pc) +
                    " BRCH only supports simple if-else (withCnt=0); match-case (withCnt>0) not "
                    "yet implemented, got " +
                    std::to_string(bc.withCnt()));
            // ASSERT(slotInRange(bc.nargs()[0]), "BRCH cond slot index out of range");
            int rc = alloc.regForSlot(bc.nargs()[0]);
            offset += (rc >= 0 ? 3 : frameAccessSize(bc.nargs()[0])) + 3 + 6; // mov + test + jnz
            break;
        }
        // JOIN: 按 nargs[0] 的分支索引从 wargs[0..withCnt-1] 中选一个写回 result。withCnt==2 即
        // if-else 两路合并
        case OpCode::JOIN: {
            if (bc.withCnt() != 2)
                return fail(
                    "pc=" + std::to_string(pc) +
                    " JOIN only supports withCnt=2 (if-else); match-case (withCnt>2) not yet "
                    "implemented, got " +
                    std::to_string(bc.withCnt()));
            // ASSERT(bc.normCnt() >= 1 && slotInRange(bc.nargs()[0]), "JOIN branch index slot out
            // of range"); ASSERT(slotInRange(bc.wargs()[0]) && slotInRange(bc.wargs()[1]) &&
            // slotInRange(bc.result),
            //        "JOIN wargs/result slot index out of range");
            int rIdx = alloc.regForSlot(bc.nargs()[0]);
            int r0   = alloc.regForSlot(bc.wargs()[0]);
            int r1   = alloc.regForSlot(bc.wargs()[1]);
            int rr   = alloc.regForSlot(bc.result);
            // r0/r1==0 时从 slot 加载（不能再用 rax），故按“从内存加载+3”计长
            size_t load0 =
                (r0 != 0 && r0 >= 0) ? (3 + 3) : (operandLoadSize(bc.wargs()[0], -1) + 3);
            size_t load1 =
                (r1 != 0 && r1 >= 0) ? (3 + 3) : (operandLoadSize(bc.wargs()[1], -1) + 3);
            offset += load0 + load1 + operandLoadSize(bc.nargs()[0], rIdx) + 3 + 4 + 3 +
                      (rr >= 0 ? 3 : frameAccessSize(bc.result));
            break;
        }
        case OpCode::FUNC:
            if (!unit.trampolineFunc)
                return fail("pc=" + std::to_string(pc) + " no FUNC trampoline");
            // ASSERT(slotInRange(bc.result), "FUNC result slot index out of range");
#if defined(_WIN32) || defined(_WIN64)
            offset += 3 + 3 + 6 + 10 + 2 + frameAccessSize(bc.result); // callTrampolineWin64 + mov
#else
            offset += 7 + 10 + 2 + frameAccessSize(bc.result); // callTrampolineSysV + mov
#endif
            break;
        case OpCode::TAIL:
            if (!unit.trampolineTail)
                return fail("pc=" + std::to_string(pc) + " no TAIL trampoline");
            for (size_t i = 0; i < bc.normCnt(); ++i)
            // ASSERT(slotInRange(bc.operands()[i]), "TAIL operand slot index out of range");
#if defined(_WIN32) || defined(_WIN64)
                offset += 3 + 3 + 6 + 10 + 2 + 1; // callTrampolineWin64 + ret
#else
                offset += 7 + 10 + 2 + 1; // callTrampolineSysV + ret
#endif
            break;
        case OpCode::JUMP:
            offset += 5;
            break;
        case OpCode::RETN: {
            // ASSERT(slotInRange(bc.fastop[0]), "RETN slot index out of range");
            int r0 = alloc.regForSlot(bc.fastop[0]);
            offset += (r0 >= 0 ? 3 : frameAccessSize(bc.fastop[0])) + 1;
            break;
        }
        case OpCode::OPER:
            if (!unit.trampolineOper)
                return fail("pc=" + std::to_string(pc) + " no OPER trampoline");
            // ASSERT(slotInRange(bc.result), "OPER result slot index out of range");
#if defined(_WIN32) || defined(_WIN64)
            offset += 3 + 3 + 6 + 10 + 10 + 2 +
                      frameAccessSize(bc.result); // callTrampolineOperWin64 + mov
#else
            offset +=
                4 + 4 + 10 + 10 + 2 + frameAccessSize(bc.result); // callTrampolineOperSysV + mov
#endif
            break;
        default:
            return fail("pc=" + std::to_string(pc) + " unsupported opcode " + to_string(bc.opcode));
        }
        pc += bc.opsize;
    }

    // 第二遍：发射机器码（可选同时输出汇编）
    // 静态区基址：负 slot 索引 -k 对应 staticBase[k]（编译期已知）
    const slot_t *staticBase = unit.frameMeta->staticArea->data();

    Encoder enc(code, unit.asmOut, 0);
#if defined(_WIN32) || defined(_WIN64)
    enc.prologueWin64();
#endif
    size_t baseOffset = 0;

    auto staticSlotAddr = [&](data_idx_t idx) -> uint64_t {
        return reinterpret_cast<uint64_t>(staticBase + static_cast<size_t>(-idx));
    };

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];

        switch (bc.opcode) {
        case OpCode::LADD: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int r1 = alloc.regForSlot(bc.fastop[1]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                enc.movRaxFromFrame(d0);
            else
                enc.movRaxFromMemAt(staticSlotAddr(bc.fastop[0]));
            if (r1 >= 0)
                enc.addRaxFromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                enc.addRaxFromFrame(d1);
            else
                enc.addRaxFromMemAt(staticSlotAddr(bc.fastop[1]));
            if (rr >= 0)
                enc.movRegFromRax(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromRax(dr);
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
                enc.movRaxFromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                enc.movRaxFromFrame(d0);
            else
                enc.movRaxFromMemAt(staticSlotAddr(bc.fastop[0]));
            if (r1 >= 0)
                enc.subRaxFromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                enc.subRaxFromFrame(d1);
            else
                enc.subRaxFromMemAt(staticSlotAddr(bc.fastop[1]));
            if (rr >= 0)
                enc.movRegFromRax(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromRax(dr);
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
                enc.movXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                enc.movXmm0FromFrame(d0);
            else
                enc.movXmm0FromMemAt(staticSlotAddr(bc.fastop[0]));
            if (r1 >= 0)
                enc.addXmm0FromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                enc.addXmm0FromFrame(d1);
            else
                enc.addXmm0FromMemAt(staticSlotAddr(bc.fastop[1]));
            if (rr >= 0)
                enc.movRegFromXmm0(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromXmm0(dr);
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
                enc.movXmm0FromReg(static_cast<uint8_t>(r0));
            else if (bc.fastop[0] > 0)
                enc.movXmm0FromFrame(d0);
            else
                enc.movXmm0FromMemAt(staticSlotAddr(bc.fastop[0]));
            if (r1 >= 0)
                enc.subXmm0FromReg(static_cast<uint8_t>(r1));
            else if (bc.fastop[1] > 0)
                enc.subXmm0FromFrame(d1);
            else
                enc.subXmm0FromMemAt(staticSlotAddr(bc.fastop[1]));
            if (rr >= 0)
                enc.movRegFromXmm0(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromXmm0(dr);
            break;
        }
        case OpCode::LLE: {
            int d0 = slotDisp(bc.fastop[0]);
            int dr = slotDisp(bc.result);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            int rr = alloc.regForSlot(bc.result);
            if (r0 >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(r0));
            else
                enc.movRaxFromFrame(d0);
            enc.cmpRaxImm8Setle();
            if (rr >= 0)
                enc.movRegFromRax(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromRax(dr);
            break;
        }
        case OpCode::BRCH: {
            data_idx_t condIdx = bc.nargs()[0];
            if (condIdx <= 0)
                return fail(
                    "pc=" + std::to_string(pc) + " BRCH cond slot index must be positive, got " +
                    std::to_string(condIdx));
            int dc    = slotDisp(condIdx);
            int rc    = alloc.regForSlot(condIdx);
            size_t t1 = pc + bc.opsize + 1;
            int32_t rel1 =
                static_cast<int32_t>(pcToOffset[t1]) - static_cast<int32_t>(enc.here() + 4 + 3 + 6);
            if (rc >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(rc));
            else
                enc.movRaxFromFrame(dc);
            enc.testRaxJzRel32(rel1);
            break;
        }
        case OpCode::JOIN: {
            data_idx_t idxSlot = bc.nargs()[0];
            data_idx_t w0      = bc.wargs()[0];
            data_idx_t w1      = bc.wargs()[1];
            int dIdx = slotDisp(idxSlot), d0 = slotDisp(w0), d1 = slotDisp(w1),
                dr   = slotDisp(bc.result);
            int rIdx = alloc.regForSlot(idxSlot);
            int r0   = alloc.regForSlot(w0);
            int r1   = alloc.regForSlot(w1);
            int rr   = alloc.regForSlot(bc.result);
            // wargs[0] -> rbx（r0==0 时 rax 未必是 slot 值，必须从 slot 加载）
            if (r0 != 0 && r0 >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(r0));
            else if (w0 > 0)
                enc.movRaxFromFrame(d0);
            else
                enc.movRaxFromMemAt(staticSlotAddr(w0));
            enc.movRegFromRax(3);
            // wargs[1] -> rcx（r1==0 时不能再用 movRaxFromReg(0)，rax 已被 wargs[0] 占用，必须从
            // slot 加载）
            if (r1 != 0 && r1 >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(r1));
            else if (w1 > 0)
                enc.movRaxFromFrame(d1);
            else
                enc.movRaxFromMemAt(staticSlotAddr(w1));
            enc.movRegFromRax(1);
            // 分支索引 -> rax
            if (rIdx >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(rIdx));
            else if (idxSlot > 0)
                enc.movRaxFromFrame(dIdx);
            else
                enc.movRaxFromMemAt(staticSlotAddr(idxSlot));
            enc.testRaxRax();
            enc.cmoveRcxFromRbx();
            enc.movRaxFromReg(1);
            if (rr >= 0)
                enc.movRegFromRax(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromRax(dr);
            break;
        }
        case OpCode::FUNC: {
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineFunc);
#if defined(_WIN32) || defined(_WIN64)
            enc.callTrampolineWin64(static_cast<uint32_t>(pc), addr);
#else
            enc.callTrampolineSysV(static_cast<uint32_t>(pc), addr);
#endif
            int dr = slotDisp(bc.result);
            enc.movFrameFromRax(dr);
            break;
        }
        case OpCode::TAIL: {
            uint64_t addr = reinterpret_cast<uint64_t>(unit.trampolineTail);
#if defined(_WIN32) || defined(_WIN64)
            enc.callTrampolineWin64(static_cast<uint32_t>(pc), addr);
#else
            enc.callTrampolineSysV(static_cast<uint32_t>(pc), addr);
#endif
            enc.ret();
            break;
        }
        case OpCode::JUMP: {
            size_t target = static_cast<size_t>(bc.fastop[0]);
            int32_t rel   = static_cast<int32_t>(pcToOffset[target] + baseOffset) -
                          static_cast<int32_t>(enc.here());
            enc.jmpRel32(rel);
            break;
        }
        case OpCode::RETN: {
            int d0 = slotDisp(bc.fastop[0]);
            int r0 = alloc.regForSlot(bc.fastop[0]);
            if (r0 >= 0)
                enc.movRaxFromReg(static_cast<uint8_t>(r0));
            else
                enc.movRaxFromFrame(d0);
            enc.ret();
            break;
        }
        case OpCode::OPER: {
            uint64_t addr     = reinterpret_cast<uint64_t>(unit.trampolineOper);
            uint64_t graphPtr = reinterpret_cast<uint64_t>(unit.graph);
#if defined(_WIN32) || defined(_WIN64)
            enc.callTrampolineOperWin64(static_cast<uint32_t>(pc), graphPtr, addr);
#else
            enc.callTrampolineOperSysV(static_cast<uint32_t>(pc), graphPtr, addr);
#endif
            int dr = slotDisp(bc.result);
            enc.movFrameFromRax(dr);
            break;
        }
        default:
            return fail("pc=" + std::to_string(pc) + " unsupported opcode " + to_string(bc.opcode));
        }

        pc += bc.opsize;
    }
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
