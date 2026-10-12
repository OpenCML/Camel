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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"
#include "passes/sched/fastvm/bytecode.h"

#include <ostream>
#include <unordered_map>

namespace camel::jit {

namespace ctx = camel::core::context;

struct CompilationDebugOptions {
    std::ostream *asmOut = nullptr; // If set, emit assembly instructions during compilation.
    // If set, fill each instruction's (start offset, length, assembly text) after compilation for
    // bindump instruction-level disassembly.
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries = nullptr;
    std::ostream *mirOut =
        nullptr;              // If set, only output MIR and return (without encoding machine code).
    bool mirSlotOnly = false; // If true and mirOut is set: output rmir (unoptimized vreg MIR) and
                              // return immediately; if false and mirOut is set: output mir
                              // (optimized vreg MIR) and return.
    bool enableDebugTrace = false; // DebugTrace MIR is disabled in normal runtime; enable it only
                                   // in dump / diagnostic mode.
    // MIR-readable printing: address/constant -> symbol name (for example trampolineFunc, static
    // slot -1); disp -> slot name (for example "n"). When mirOut is non-null, backend may write
    // static-slot comments into mirSymbolNames (caller passes a non-const pointer).
    std::unordered_map<uint64_t, std::string> *mirSymbolNames = nullptr;
    const std::unordered_map<int, std::string> *mirSlotNames  = nullptr;
};

struct CompilationUnit {
    camel::runtime::GCGraph *runtimeGraph = nullptr;
    std::span<const Bytecode> bytecodes;
    size_t entryPc;
    size_t graphLength                   = 0;
    void *trampolineFunc                 = nullptr; // FUNC trampoline
    void *trampolineTail                 = nullptr; // TAIL trampoline
    void *trampolineOper                 = nullptr; // OPER trampoline
    void *trampolineCast                 = nullptr; // CAST trampoline
    void *trampolineBytecode             = nullptr; // slow-path bytecode trampoline
    void *poolTopAddr                    = nullptr; // &FramePool::top_ for inline frame mgmt
    void *directSelfFuncInvokeAddr       = nullptr; // slow path fallback
    // The byte that is not zero while the collector waits for a safepoint: framed code polls
    // it where the interpreter polls (a self tail call, which is a jump in compiled code).
    const void *safepointFlagAddr = nullptr;
    const CompilationDebugOptions *debug = nullptr;
};

struct RelocInfo {
    size_t offset; // Byte offset in machine code.
    uint32_t kind; // Relocation type.
    size_t target; // Target pc or trampoline id.
};

struct CompiledCode {
    std::vector<uint8_t> code;
    size_t entryOffset;
    size_t jitEntryOffset = 0; // offset to JIT-internal entry (skips C++ ABI wrapper)
    // Framed code (x64::JitExitKind): it leaves to a driver for calls and must be run by one.
    bool framed = false;
    std::vector<RelocInfo> relocs;
};

using JitEntryFn = slot_t (*)(slot_t *slots, void *ctx);

// One construct of a graph's bytecode that keeps the graph from being compiled, or from being
// compiled as frameless code. `kind` is a stable short name for counting (OPER, CAST, COPY, FILL,
// CALL, ACCS, FUNC-other-graph, TAIL-other-graph, BRCH-match, JOIN-match, SCHD, frame-too-large,
// no-static-area, ...); `text` is the sentence the compile log gives.
struct JitBlocker {
    size_t pc = 0;
    std::string kind;
    std::string text;
};

class IJitBackend {
  public:
    virtual ~IJitBackend() = default;

    virtual std::unique_ptr<CompiledCode>
    compile(const CompilationUnit &unit, std::string *failureReason = nullptr) = 0;
    /// Every construct of the unit this back end does not compile, in bytecode order: what the
    /// coverage statistics count (FastVMSchedPass::writeStats). compile() fails with the first
    /// of them; it can also fail later (register allocation) with an empty list here.
    virtual std::vector<JitBlocker> blockers(const CompilationUnit &unit) const = 0;
    /// Every construct of the unit that needs the runtime, in bytecode order. With none the
    /// unit is compiled as frameless code, otherwise as framed code (x64::JitExitKind).
    virtual std::vector<JitBlocker> runtimeConstructs(const CompilationUnit &unit) const = 0;
    virtual void registerTrampoline(const char *name, void *addr)              = 0;
    virtual JitEntryFn load(std::unique_ptr<CompiledCode> code)                = 0;
    virtual void unload(JitEntryFn fn)                                         = 0;
};

std::unique_ptr<IJitBackend> createBackend();

} // namespace camel::jit
