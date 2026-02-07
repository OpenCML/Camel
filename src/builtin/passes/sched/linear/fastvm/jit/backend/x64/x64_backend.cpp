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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "x64_backend.h"

#include "builtin/passes/sched/linear/fastvm/jit/regalloc/regalloc.h"
#include "core/context/frame.h"
#include "core/rtdata/data.h"
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
    return static_cast<int>(frameBaseOffset_ + static_cast<size_t>(idx) * sizeof(slot_t));
}

void X64Backend::registerTrampoline(const char *name, void *addr) { trampolines_[name] = addr; }

std::unique_ptr<CompiledCode> X64Backend::compile(const CompilationUnit &unit) {
    auto result = std::make_unique<CompiledCode>();
    if (!compileBytecode(unit, result->code))
        return nullptr;
    result->entryOffset = 0;
    return result;
}

bool X64Backend::compileBytecode(const CompilationUnit &unit, std::vector<uint8_t> &code) {
    using namespace x64;

    if (!unit.frameMeta) {
        EXEC_WHEN_DEBUG(
            l.in("JIT.Backend")
                .error("compileBytecode: graph '{}' has no FrameMeta", unit.graph->name()));
        return false;
    }

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
    auto frameAccessSize = [&](int idx) {
        int d = slotDisp(idx);
        return x64::Encoder::fitsDisp8(d) ? 4 : 7;
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
            if (!slotInRange(bc.fastop[0]) || !slotInRange(bc.fastop[1]) || !slotInRange(bc.result))
                return false;
            int r0 = alloc.regForSlot(bc.fastop[0]), r1 = alloc.regForSlot(bc.fastop[1]),
                rr = alloc.regForSlot(bc.result);
            offset += (r0 >= 0 ? 3 : frameAccessSize(bc.fastop[0])) +
                      (r1 >= 0 ? 3 : frameAccessSize(bc.fastop[1])) +
                      (rr >= 0 ? 3 : frameAccessSize(bc.result));
            break;
        }
        case OpCode::LLE: {
            if (bc.fastop[1] != -1)
                return false; // 仅支持 n<=1 (rhs=常量1)
            if (!slotInRange(bc.fastop[0]) || !slotInRange(bc.result))
                return false;
            int r0 = alloc.regForSlot(bc.fastop[0]), rr = alloc.regForSlot(bc.result);
            offset += (r0 >= 0 ? 3 : frameAccessSize(bc.fastop[0])) + 5 + 4 +
                      (rr >= 0 ? 3 : frameAccessSize(bc.result)); // mov + cmp/setle/movzx + mov
            break;
        }
        case OpCode::BRCH: {
            if (bc.withCnt() != 0)
                return false; // 仅支持简单 if-else
            if (!slotInRange(bc.nargs()[0]))
                return false;
            int rc = alloc.regForSlot(bc.nargs()[0]);
            offset += (rc >= 0 ? 3 : frameAccessSize(bc.nargs()[0])) + 3 + 6; // mov + test + jnz
            break;
        }
        case OpCode::FUNC:
            if (!unit.trampolineFunc)
                return false;
            if (!slotInRange(bc.result))
                return false;
#if defined(_WIN32) || defined(_WIN64)
            offset += 3 + 3 + 6 + 10 + 2 + frameAccessSize(bc.result); // callTrampolineWin64 + mov
#else
            offset += 7 + 10 + 2 + frameAccessSize(bc.result); // callTrampolineSysV + mov
#endif
            break;
        case OpCode::TAIL:
            if (!unit.trampolineTail)
                return false;
            for (size_t i = 0; i < bc.normCnt(); ++i)
                if (!slotInRange(bc.operands()[i]))
                    return false;
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
            if (!slotInRange(bc.fastop[0]))
                return false;
            int r0 = alloc.regForSlot(bc.fastop[0]);
            offset += (r0 >= 0 ? 3 : frameAccessSize(bc.fastop[0])) + 1;
            break;
        }
        case OpCode::OPER:
            if (!unit.trampolineOper)
                return false;
            if (!slotInRange(bc.result))
                return false;
#if defined(_WIN32) || defined(_WIN64)
            offset += 3 + 3 + 6 + 10 + 10 + 2 +
                      frameAccessSize(bc.result); // callTrampolineOperWin64 + mov
#else
            offset +=
                4 + 4 + 10 + 10 + 2 + frameAccessSize(bc.result); // callTrampolineOperSysV + mov
#endif
            break;
        default:
            return false;
        }
        pc += bc.opsize;
    }

    // 第二遍：发射机器码（可选同时输出汇编）
    Encoder enc(code, unit.asmOut, 0);
#if defined(_WIN32) || defined(_WIN64)
    enc.prologueWin64();
#endif
    size_t baseOffset = 0;

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
            else
                enc.movRaxFromFrame(d0);
            if (r1 >= 0)
                enc.addRaxFromReg(static_cast<uint8_t>(r1));
            else
                enc.addRaxFromFrame(d1);
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
            else
                enc.movRaxFromFrame(d0);
            if (r1 >= 0)
                enc.subRaxFromReg(static_cast<uint8_t>(r1));
            else
                enc.subRaxFromFrame(d1);
            if (rr >= 0)
                enc.movRegFromRax(static_cast<uint8_t>(rr));
            else
                enc.movFrameFromRax(dr);
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
                return false;
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
            return false;
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
