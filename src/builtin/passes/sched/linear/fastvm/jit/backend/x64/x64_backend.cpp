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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "x64_backend.h"

#include "core/context/frame.h"
#include "core/rtdata/data.h"

#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace camel::jit {

namespace {

size_t getFrameDynamicAreaOffset() { return sizeof(Frame); }

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

    const Bytecode *base = unit.bytecodes.data();
    size_t pcEnd         = unit.bytecodes.size();
    size_t entryPc       = unit.entryPc;

    // 第一遍：计算每条字节码对应的机器码偏移
    std::unordered_map<size_t, size_t> pcToOffset;
    size_t offset = 0;
    for (size_t pc = entryPc; pc < pcEnd;) {
        pcToOffset[pc]     = offset;
        const Bytecode &bc = base[pc];
        switch (bc.opcode) {
        case OpCode::LADD:
        case OpCode::LSUB:
            offset += 3 * 4; // mov+op+mov
            break;
        case OpCode::JUMP:
            offset += 5; // jmp rel32
            break;
        case OpCode::RETN:
            offset += 4 + 1; // mov rax + ret
            break;
        default:
            return false; // 含不支持的指令，放弃 JIT
        }
        pc += bc.opsize;
    }

    // 第二遍：发射机器码
    Encoder enc(code);
    size_t baseOffset = 0;

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];

        switch (bc.opcode) {
        case OpCode::LADD: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            enc.movRaxFromFrame(d0);
            enc.addRaxFromFrame(d1);
            enc.movFrameFromRax(dr);
            break;
        }
        case OpCode::LSUB: {
            int d0 = slotDisp(bc.fastop[0]);
            int d1 = slotDisp(bc.fastop[1]);
            int dr = slotDisp(bc.result);
            enc.movRaxFromFrame(d0);
            enc.subRaxFromFrame(d1);
            enc.movFrameFromRax(dr);
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
            enc.movRaxFromFrame(d0);
            enc.ret();
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
