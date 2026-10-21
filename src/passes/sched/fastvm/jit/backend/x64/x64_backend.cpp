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
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/profile/profile.h"
#include "camel/runtime/exec_plan.h"
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

// A BRCH is followed by its jump table, one JUMP per arm: two for if-else; for a match one per
// case value and then the default arm, when the match has one. This is the number of entries
// that are certainly there; the lowering of a match also names the entry behind them, which it
// reaches only when the match has a default arm.
inline size_t branchTableEntries(const Bytecode &bc) {
    return bc.withCnt() == 0 ? 2 : bc.withCnt();
}

// What keeps a unit out of the compiler altogether, in bytecode order. With `firstOnly` the scan
// stops at the first finding (what compile() reports); without, every finding is listed (what
// the coverage statistics count). Framed code takes every bytecode, so what is left is a unit
// without the layout the compiler reads.
std::vector<JitBlocker> collectBlockers(const CompilationUnit &unit, bool firstOnly) {
    std::vector<JitBlocker> found;
    auto add = [&](size_t pc, std::string kind, std::string text) {
        found.push_back({pc, std::move(kind), std::move(text), {}});
    };
    if (!unit.runtimeGraph) {
        add(0, "no-graph", "null runtime graph in JIT compilation unit");
        return found;
    }
    if (!unit.runtimeGraph->hasFrameLayout()) {
        add(
            0,
            "no-frame-layout",
            "incomplete frame layout for graph '" + unit.runtimeGraph->name() + "'");
        return found;
    }
    if (unit.graphLength == 0) {
        add(0, "empty", "graph bytecode span is empty");
        return found;
    }
    const size_t graphEnd = unit.entryPc + unit.graphLength;
    ASSERT(
        graphEnd <= unit.bytecodes.size(),
        "Current graph bytecode range exceeds linked bytecode span.");
    for (size_t pc = unit.entryPc; pc < graphEnd;) {
        if (firstOnly && !found.empty())
            return found;
        const Bytecode &bc = unit.bytecodes[pc];
        if (bc.opcode == OpCode::BRCH) {
            // The lowering jumps to the entries of the jump table.
            const size_t table = pc + bc.opsize;
            bool complete = table + bc.withCnt() < graphEnd;
            for (size_t arm = 0; complete && arm < branchTableEntries(bc); ++arm) {
                complete = unit.bytecodes[table + arm].opcode == OpCode::JUMP;
            }
            if (!complete) {
                add(pc,
                    "BRCH-table",
                    std::format("pc={} BRCH is not followed by one JUMP for each arm", pc));
            }
        }
        pc += bc.opsize;
    }
    return found;
}

// Arguments of a call that are kept in registers at once; a call with more copies them one by
// one through memory (see FUNC and TAIL in compileBytecodeWith).
constexpr size_t kMaxRegisterArgs = 4;

// A loop of regions whose three events (LBEG, LTST, LEND) are in the unit, and whether the rounds
// of it are lowered to native code instead of calls of the loop helpers of trampoline.h (H55).
// The pcs are the ones the events were emitted at (compile.cpp): the LBEG starts the loop, the
// LTST runs after the condition's region, the LEND after the body's region, and the LEND jumps
// back to the first instruction of a round when another one follows.
struct LoopRound {
    const camel::runtime::ExecLoop *loop = nullptr;
    size_t beginPc = SIZE_MAX; // LBEG
    size_t testPc  = SIZE_MAX; // LTST
    size_t endPc   = SIZE_MAX; // LEND
    size_t bodyPc  = SIZE_MAX; // the first instruction of the body (the LTST falls through to it)
    size_t roundPc = SIZE_MAX; // the first instruction of a round (the LEND jumps back to it)
    // The pc this lowering gives the first instruction of the stop path. No bytecode is at it: it
    // is a local label (the MIR jumps by pc), one per loop, above every graph's bytecode.
    uint32_t stopPc = 0;
    std::string helperReason; // why the rounds take the helpers; empty when they are native
    bool native() const { return helperReason.empty(); }
};

// Where the stop path of the nth loop goes in MIR (LoopRound::stopPc): a pc no bytecode has, so
// that a local branch can target an instruction in the middle of a bytecode's lowering.
constexpr uint32_t kLocalLabelPcBase = 0x7F000000u;

// What a native round may consist of: an instruction that neither allocates, so that no
// collection can be asked for inside the round, nor reaches a safepoint or another graph.
// Arithmetic, comparison, branch and join do not; the divisions run natively and fault through
// the trap check they already carry (markDivision), which is the driver's; the integer remainder
// is the one helper call allowed, because it stores its own result, does not allocate and does
// not poll. Everything that builds a value the collector traces — an operator, a cast, a copy
// (which clones), a fill, an element access — a call, and another loop's events keep the round
// on the helpers.
bool instructionRunsInRound(OpCode op) {
    switch (op) {
    case OpCode::IADD:
    case OpCode::LADD:
    case OpCode::FADD:
    case OpCode::DADD:
    case OpCode::ISUB:
    case OpCode::LSUB:
    case OpCode::FSUB:
    case OpCode::DSUB:
    case OpCode::IMUL:
    case OpCode::LMUL:
    case OpCode::FMUL:
    case OpCode::DMUL:
    case OpCode::IDIV:
    case OpCode::LDIV:
    case OpCode::FDIV:
    case OpCode::DDIV:
    case OpCode::IMOD:
    case OpCode::LMOD:
    case OpCode::ILT:
    case OpCode::LLT:
    case OpCode::FLT:
    case OpCode::DLT:
    case OpCode::IGT:
    case OpCode::LGT:
    case OpCode::FGT:
    case OpCode::DGT:
    case OpCode::IEQ:
    case OpCode::LEQ:
    case OpCode::FEQ:
    case OpCode::DEQ:
    case OpCode::INE:
    case OpCode::LNE:
    case OpCode::FNE:
    case OpCode::DNE:
    case OpCode::ILE:
    case OpCode::LLE:
    case OpCode::FLE:
    case OpCode::DLE:
    case OpCode::IGE:
    case OpCode::LGE:
    case OpCode::FGE:
    case OpCode::DGE:
    case OpCode::BRCH:
    case OpCode::JOIN:
    case OpCode::JUMP:
        return true;
    default:
        return false;
    }
}

bool isLoopEvent(OpCode op) {
    return op == OpCode::LBEG || op == OpCode::LTST || op == OpCode::LEND;
}

// The loops of the unit's own bytecode, in the order their events appear, each with the pcs the
// lowering of a round reads and the reason it cannot be native ("" when it can). The single place
// that decides this: the coverage statistics and the lowerer both call it, so what the statistics
// say a loop does is what the code does.
std::vector<LoopRound> loopRoundsOf(const CompilationUnit &unit) {
    std::vector<LoopRound> rounds;
    if (!unit.runtimeGraph || unit.graphLength == 0)
        return rounds;
    const Bytecode *base = unit.bytecodes.data();
    const size_t graphEnd = unit.entryPc + unit.graphLength;
    ASSERT(
        graphEnd <= unit.bytecodes.size(),
        "Current graph bytecode range exceeds linked bytecode span.");
    std::unordered_map<const camel::runtime::ExecLoop *, size_t> indexOf;
    for (size_t pc = unit.entryPc; pc < graphEnd;) {
        const Bytecode &bc = base[pc];
        if (isLoopEvent(bc.opcode)) {
            const camel::runtime::ExecLoop *loop = bc.extra()->loop;
            if (loop != nullptr) {
                auto it = indexOf.find(loop);
                if (it == indexOf.end()) {
                    it = indexOf.emplace(loop, rounds.size()).first;
                    rounds.push_back({});
                    rounds.back().loop = loop;
                }
                LoopRound &round = rounds[it->second];
                switch (bc.opcode) {
                case OpCode::LBEG: round.beginPc = pc; break;
                case OpCode::LTST: round.testPc = pc; break;
                case OpCode::LEND: round.endPc = pc; break;
                default: break;
                }
            }
        }
        pc += bc.opsize;
    }
    for (size_t i = 0; i < rounds.size(); ++i) {
        LoopRound &round = rounds[i];
        round.stopPc    = kLocalLabelPcBase + static_cast<uint32_t>(i);
        const camel::runtime::ExecLoop &loop = *round.loop;
        auto why = [&](std::string reason) { round.helperReason = std::move(reason); };
        if (round.beginPc == SIZE_MAX || round.testPc == SIZE_MAX || round.endPc == SIZE_MAX) {
            why("the loop's events are not all in the graph's bytecode");
            continue;
        }
        if (getJumpTargetPc(&base[round.testPc]) != round.endPc ||
            round.beginPc + base[round.beginPc].opsize != getJumpTargetPc(&base[round.endPc])) {
            why("the loop's events do not link start, test and end as the lowering reads them");
            continue;
        }
        // What the loop is, before where its events are: a shape that cannot go native says so,
        // whatever the bytecode around it looks like (a loop over a sequence starts and tests at
        // the same place, and has no condition of its own).
        if (loop.iterates) {
            why("the round takes the next element of a sequence");
            continue;
        }
        if (!loop.hasCond) {
            why("the loop has no condition to test");
            continue;
        }
        if (loop.collectionCount != 0) {
            why("the round collects the values it produces");
            continue;
        }
        if (!loop.stateComponents) {
            why(
                loop.stateCount == 1
                    ? "the state is a single value, not a slot the body writes every round"
                    : "the round makes the state tuple anew every round");
            continue;
        }
        round.roundPc = getJumpTargetPc(&base[round.endPc]);
        round.bodyPc  = round.testPc + base[round.testPc].opsize;
        if (round.roundPc >= round.testPc || round.bodyPc >= round.endPc) {
            why("the loop has no condition or body instruction of its own");
            continue;
        }
        if (round.endPc + base[round.endPc].opsize >= graphEnd) {
            why("the loop is the last instruction of the graph");
            continue;
        }
        // The state binding and the round count are slot moves with no runtime behind them, so the
        // slots must be the frame's own: the ports and the components the body writes (a component
        // of the static area is not one of those, planStateComponents), and a round slot that is a
        // value slot and not slot 0, the frame pointer of the JIT's frame.
        bool slotsInFrame = loop.roundSlot > 0 && loop.condResultSlot != 0;
        for (size_t j = loop.stateCount; slotsInFrame && j < loop.stateCount * 3; ++j)
            slotsInFrame = loop.slots[j] > 0;
        if (slotsInFrame)
            for (size_t j = 0; j < loop.stateCount; ++j)
                slotsInFrame = slotsInFrame && loop.stateSlots[j] > 0;
        if (!slotsInFrame) {
            why("the loop's state or round count is not in a frame slot of its own");
            continue;
        }
        if (camel::profile::enabled()) {
            why("std::profile counts the rounds");
            continue;
        }
        if (camel::core::mm::autoSpace().debugConfig().stressEveryNSafepoints != 0) {
            why("the safepoint stress mode asks for a safepoint every N safepoints");
            continue;
        }
        // The instructions of a round: the condition's region, the LTST, the body's region. Any
        // instruction that could allocate (and so ask for a collection) or reach a safepoint
        // keeps the round on the helpers, because only the helper path polls.
        for (size_t pc = round.roundPc; pc < round.endPc; pc += base[pc].opsize) {
            const OpCode op = base[pc].opcode;
            if (op == OpCode::LTST)
                continue; // this loop's own test
            if (isLoopEvent(op)) {
                why(std::format("the round contains a nested loop at pc={}", pc));
                break;
            }
            if (!instructionRunsInRound(op)) {
                why(std::format("the round contains {} at pc={}", to_string(op), pc));
                break;
            }
        }
    }
    return rounds;
}

// What keeps a unit from being compiled as frameless code (mir.h, JitExitKind): every construct
// that needs the runtime. The single place that says which code a unit gets: none of these,
// frameless; any, framed.
std::vector<JitBlocker> collectRuntimeConstructs(const CompilationUnit &unit, bool firstOnly) {
    std::vector<JitBlocker> found;
    auto add = [&](size_t pc, std::string kind, std::string text) {
        found.push_back({pc, std::move(kind), std::move(text), {}});
    };
    if (!unit.runtimeGraph || !unit.runtimeGraph->hasFrameLayout() || unit.graphLength == 0) {
        return found; // not compiled at all (collectBlockers)
    }
    if (!unit.runtimeGraph->staticArea()) {
        // Frameless code reads its constants at compile time and expects the area to exist.
        add(0, "no-static-area", "graph '" + unit.runtimeGraph->name() + "' has no static area");
        return found;
    }
    // Frameless code has no stack maps: the collector neither sees nor updates what a native
    // frame or a register holds. Two things keep that sound with the moving collector.
    //
    // 1. No collection runs while a native frame exists. Frameless code never calls into C++
    //    and contains no safepoint poll, so between the entry into it and the return from it
    //    the collector cannot run (std::jit is single-threaded). A native frame may therefore
    //    hold object references, e.g. a var parameter that a self call passes on. The
    //    allocator refuses a safepoint reached under frameless code
    //    (mm::detail::nativeCodeDepth, counted in FastVMSchedPass::invokeOwnedJitFrame).
    // 2. The code outlives any number of collections, so it must not contain the address of an
    //    object. When objects can move, an object constant is read from the static area each
    //    time (loadStaticConst in compileBytecodeWith): the area is a root the collector
    //    updates in place. The one place that has no such load is a call or self tail call
    //    with more arguments than are passed in registers, which copies constants as
    //    immediates; a graph that passes an object constant there is framed code.
    const bool objectsMove          = camel::core::mm::autoSpace().youngGenCopyingEnabled();
    const auto *staticLayoutOfGraph = unit.runtimeGraph->staticDataType();
    auto passesObjectConstantInMemory = [&](const Bytecode &bc) {
        if (!objectsMove || staticLayoutOfGraph == nullptr)
            return false;
        const data_arr_t srcArgs  = bc.directCallSrcArgs();
        const data_arr_t dstSlots = bc.directCallDstSlots();
        size_t moved              = 0;
        bool objectConstant       = false;
        for (size_t i = 0; i < srcArgs.size() && i < dstSlots.size(); ++i) {
            if (bc.opcode == OpCode::TAIL && srcArgs[i] == dstSlots[i])
                continue;
            ++moved;
            const auto index = static_cast<size_t>(-srcArgs[i]);
            objectConstant   = objectConstant ||
                             (srcArgs[i] < 0 && index < staticLayoutOfGraph->size() &&
                              camel::core::type::isGCTraced(staticLayoutOfGraph->codeAt(index)));
        }
        return objectConstant && moved > kMaxRegisterArgs;
    };
    const size_t graphEnd = unit.entryPc + unit.graphLength;
    ASSERT(
        graphEnd <= unit.bytecodes.size(),
        "Current graph bytecode range exceeds linked bytecode span.");
    const size_t frameBytes =
        (unit.runtimeGraph->runtimeDataType()->size() * sizeof(slot_t) + 15u) & ~size_t{15};
    // The lowering of the loops of the graph (LoopRound), once: what a LBEG reports below.
    const std::vector<LoopRound> loopRounds = loopRoundsOf(unit);
    for (size_t pc = unit.entryPc; pc < graphEnd;) {
        if (firstOnly && !found.empty())
            return found;
        const Bytecode &bc      = unit.bytecodes[pc];
        const std::string where = "pc=" + std::to_string(pc);
        const std::string fallback = "graph needs the runtime: " + where;
        switch (bc.opcode) {
        case OpCode::OPER:
            add(pc, "OPER", fallback + " contains OPER");
            break;
        case OpCode::CAST:
            add(pc, "CAST", fallback + " contains CAST");
            break;
        case OpCode::COPY:
            add(pc, "COPY", fallback + " contains COPY");
            break;
        case OpCode::FILL:
            add(pc, "FILL", fallback + " contains FILL");
            break;
        case OpCode::CALL:
            add(pc, "CALL", fallback + " contains indirect CALL");
            break;
        case OpCode::ACCS:
            add(pc, "ACCS", fallback + " contains ACCS");
            break;
        case OpCode::SCHD:
            add(pc, "SCHD", where + " unsupported opcode SCHD");
            break;
        case OpCode::LBEG:
        case OpCode::LTST:
        case OpCode::LEND: {
            // The rounds run in the frame; a graph with a loop is framed code. One entry per
            // loop, at its LBEG: the entry says whether the rounds of it are lowered to native
            // code (LoopRound) or go through the loop helpers, and why.
            if (bc.opcode != OpCode::LBEG)
                break;
            const LoopRound *round = nullptr;
            for (const LoopRound &candidate : loopRounds) {
                if (candidate.loop == bc.extra()->loop) {
                    round = &candidate;
                    break;
                }
            }
            const bool isNative = round != nullptr && round->native();
            const std::string what =
                isNative
                    ? where + " contains a loop whose rounds are native code"
                    : fallback + " contains a loop (LBEG/LTST/LEND)" +
                          (round != nullptr && !round->helperReason.empty()
                               ? ": " + round->helperReason
                               : std::string());
            std::string lowering = "helper";
            if (round != nullptr) {
                lowering = isNative ? "native" : "helper: " + round->helperReason;
            }
            found.push_back({pc, "LOOP", what, lowering});
            break;
        }
        case OpCode::IMOD:
        case OpCode::LMOD:
            // The remainder runs in the helper (it faults on a zero divisor), so the graph
            // needs the frame a helper call runs against.
            add(pc, "IMOD", fallback + " contains an integer remainder");
            break;
        case OpCode::FUNC:
        case OpCode::TAIL: {
            const auto *target = getFuncExtraRuntimeGraph(&bc);
            if (target != unit.runtimeGraph) {
                add(pc,
                    bc.opcode == OpCode::FUNC ? "FUNC-other-graph" : "TAIL-other-graph",
                    std::format(
                        "graph needs the runtime: pc={} targets non-self graph "
                        "'{}' ({:p})",
                        pc,
                        target ? target->name() : "<null>",
                        static_cast<const void *>(target)));
            } else if (passesObjectConstantInMemory(bc)) {
                add(pc,
                    "object-constant-arg",
                    std::format(
                        "graph needs the runtime: pc={} passes an object constant among more "
                        "arguments than fit registers, and the collector moves objects",
                        pc));
            } else if (bc.opcode == OpCode::FUNC && frameBytes > x64::kMaxNativeFrameBytes) {
                add(pc,
                    "frame-too-large",
                    std::format(
                        "pc={} self call with a frame of {} bytes (more than {}): the recursion "
                        "would not fit the machine stack at the recursion limit",
                        pc,
                        frameBytes,
                        x64::kMaxNativeFrameBytes));
            }
            break;
        }
        case OpCode::BRCH:
            if (bc.withCnt() != 0)
                add(pc,
                    "BRCH-match",
                    where +
                        " BRCH only supports simple if-else (withCnt=0); match-case (withCnt>0) "
                        "not yet implemented, got " +
                        std::to_string(bc.withCnt()));
            break;
        case OpCode::JOIN:
            if (bc.result != 0 && bc.extra()->pType != camel::core::type::Type::Void() &&
                bc.withCnt() != 2)
                add(pc,
                    "JOIN-match",
                    where +
                        " JOIN only supports withCnt=2 (if-else); match-case (withCnt>2) not yet "
                        "implemented, got " +
                        std::to_string(bc.withCnt()));
            break;
        default:
            break;
        }
        pc += bc.opsize;
    }
    return found;
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

std::vector<JitBlocker> X64Backend::blockers(const CompilationUnit &unit) const {
    return collectBlockers(unit, false);
}

std::vector<JitBlocker> X64Backend::runtimeConstructs(const CompilationUnit &unit) const {
    return collectRuntimeConstructs(unit, false);
}

std::unique_ptr<CompiledCode>
X64Backend::compile(const CompilationUnit &unit, std::string *failureReason) {
    auto result = std::make_unique<CompiledCode>();
    if (!compileBytecode(unit, result->code, failureReason))
        return nullptr;
    result->entryOffset    = 0;
    result->jitEntryOffset = jitEntryOffset_;
    result->framed         = framed_;
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

    // What this back end compiles, and as which of its two kinds of code (mir.h, JitExitKind),
    // is decided in two places, before anything is lowered. Frameless code never calls into
    // C++: its calls are native self calls and its tail calls are jumps. Framed code calls the
    // helpers of trampoline.h and leaves to its driver for calls of Camel functions.
    if (const auto blocked = collectBlockers(unit, true); !blocked.empty()) {
        return fail(blocked.front().text);
    }
    const bool framed = !collectRuntimeConstructs(unit, true).empty();
    framed_           = framed;

    const Bytecode *base = unit.bytecodes.data();
    size_t entryPc       = unit.entryPc;
    // Only this graph's bytecode: what follows belongs to other graphs (among them graphs
    // compiled at run time, whose code may use slots and opcodes this graph never does).
    size_t pcEnd = unit.graphLength != 0
                       ? std::min(unit.bytecodes.size(), entryPc + unit.graphLength)
                       : unit.bytecodes.size();

    // A graph without constants has no static area; no operand of it is a static slot then.
    const slot_t *staticBase =
        unit.runtimeGraph->staticArea() ? unit.runtimeGraph->staticArea()->data() : nullptr;
    const auto *staticLayout = unit.runtimeGraph->staticDataType();
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
    // A constant is read at compile time and becomes an immediate, except an object constant
    // when the collector moves objects (the code outlives collections; the static area is
    // where the collector keeps the reference up to date) and in framed code under either
    // collector. A register that holds such a reference is not live across a safepoint:
    // frameless code has none between its entry and its return, and in framed code no value
    // lives across a helper call, an exit or a poll.
    const bool objectsMove = camel::core::mm::autoSpace().youngGenCopyingEnabled();
    auto loadStaticConst = [&](data_idx_t idx, x64::VRegId v) {
        const auto index = static_cast<size_t>(-idx);
        if ((framed || objectsMove) && staticLayout != nullptr && index < staticLayout->size() &&
            camel::core::type::isGCTraced(staticLayout->codeAt(index))) {
            build.emitVLoadFromMemAt(v, reinterpret_cast<uint64_t>(staticBase + index));
        } else {
            emitLoadStaticConst(staticBase, idx, v, build);
        }
    };
    auto isObjectConstant = [&](data_idx_t idx) {
        const auto index = static_cast<size_t>(-idx);
        return idx < 0 && staticLayout != nullptr && index < staticLayout->size() &&
               camel::core::type::isGCTraced(staticLayout->codeAt(index));
    };
    const uint64_t helperOper      = reinterpret_cast<uint64_t>(&jitHelperOper);
    const uint64_t helperBytecode  = reinterpret_cast<uint64_t>(&jitHelperBytecode);
    const uint64_t helperBranchArm = reinterpret_cast<uint64_t>(&jitHelperBranchArm);
    const uint64_t helperLoopBegin = reinterpret_cast<uint64_t>(&jitHelperLoopBegin);
    const uint64_t helperLoopTest  = reinterpret_cast<uint64_t>(&jitHelperLoopTest);
    const uint64_t helperLoopEnd   = reinterpret_cast<uint64_t>(&jitHelperLoopEnd);
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
            for (size_t arm = 0; arm <= branchTableEntries(base[p]); ++arm)
                blockStarts.insert(next + arm);
            break;
        case OpCode::JUMP:
            blockStarts.insert(getJumpTargetPc(&base[p]));
            blockStarts.insert(next);
            break;
        case OpCode::LTST:
        case OpCode::LEND:
            // LTST jumps to its LEND when the loop stops; LEND jumps back to the first
            // instruction of a round. The slot cache must not carry a value across either.
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
            loadStaticConst(slotIdx, v);
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

    // The loops of this unit whose rounds are lowered to native code (LoopRound): the decisions
    // are made once, before anything is lowered, by the function the statistics read too.
    const std::vector<LoopRound> loopRounds = loopRoundsOf(unit);
    auto nativeRound = [&](const camel::runtime::ExecLoop *loop) -> const LoopRound * {
        for (const LoopRound &round : loopRounds)
            if (round.loop == loop && round.native())
                return &round;
        return nullptr;
    };
    // Gives the k ports at loop.slots[first] the running state. stateComponents keeps the state
    // in the body's own slots (ExecLoop::stateSlots), so a round starts and goes on by copying
    // them into the ports the condition and the body read — what bindLoopState does in the
    // interpreter, done by the frame the code already has. The copy is a move of one word: a port
    // holds whatever type the component has (a scalar's bits, or a reference).
    auto bindLoopPorts = [&](const camel::runtime::ExecLoop &loop, size_t first) {
        for (size_t j = 0; j < loop.stateCount; ++j) {
            const data_idx_t from = loop.stateSlots[j];
            x64::VRegId v         = nextVReg++;
            loadSlot(from, slotDisp(from), v, false);
            build.emitVStoreToFrame(slotDisp(loop.slots[first + j]), v);
        }
    };

    // The wrapper owns all ABI adaptation. MIR generation below always assumes
    // the internal JIT convention:
    //   rdi = current frame dynamic area
    //   rsi = JitContext*
    //   rbx = &FramePool::top_
    // Keeping the body prologue-free is what makes direct JIT-to-JIT calls cheap.

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc         = base[pc];
        const size_t mirCountBefore = mirBuf.size();
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
            int dr    = slotDisp(bc.result);
            size_t t0 = pc + bc.opsize;
            size_t t1 = pc + bc.opsize + 1;
            if (bc.withCnt() != 0) {
                // A match (framed code only): the helper compares the value with the case
                // values, as the interpreter does, stores the index of the arm and returns it;
                // the code then goes to that entry of the jump table.
                build.emitCallHelper(static_cast<uint32_t>(pc), helperBranchArm);
                x64::VRegId vArm = nextVReg++;
                build.emitVMovFromRax(vArm);
                for (size_t arm = 0; arm < bc.withCnt(); ++arm) {
                    build.emitVCmpRegImm(vArm, static_cast<int32_t>(arm));
                    build.emitJeRel32(static_cast<uint32_t>(t0 + arm));
                }
                build.emitJmpRel32(static_cast<uint32_t>(t0 + bc.withCnt()));
                slotCache.clear();
                break;
            }
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
                if (condIdx == 0)
                    return fail("pc=" + std::to_string(pc) + " BRCH has no condition operand");
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
            if (bc.withCnt() != 2) {
                // The JOIN of a match (framed code only): the helper takes the operand of the
                // arm whose index the BRCH stored.
                build.emitCallHelper(static_cast<uint32_t>(pc), helperBytecode);
                slotCache.clear();
                break;
            }
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
            if (framed) {
                // Framed code does not call: its driver makes the call (of this graph or of
                // another, compiled or not) the way the interpreter does, on a pool frame and
                // counted on the interpreter's stack, and continues the code behind this
                // bytecode with the result stored.
                build.emitExitToDriver(static_cast<uint32_t>(pc), x64::JitExitKind::Call);
                slotCache.clear();
                break;
            }
            // Every call in frameless code is a call to the graph itself
            // (collectRuntimeConstructs). It is encoded natively, in the JIT's own
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
            if (params->calleeSlotBytes > x64::kMaxNativeFrameBytes)
                return fail(std::format(
                    "pc={} self call with a frame of {} bytes (more than {}): the recursion "
                    "would not fit the machine stack at the recursion limit",
                    pc,
                    params->calleeSlotBytes,
                    x64::kMaxNativeFrameBytes));
            // Phase L: the arguments of a call with few of them are loaded by visible
            // VLoadFromFrame, so that peephole can fuse a preceding store with the load.
            const bool inRegisters = srcArgs.size() <= kMaxRegisterArgs;
            for (size_t ai = 0; ai < srcArgs.size(); ++ai) {
                NativeJitCallParams::Arg arg;
                arg.dstDisp = slotDisp(dstSlots[ai]);
                if (srcArgs[ai] < 0 && objectsMove && isObjectConstant(srcArgs[ai])) {
                    // Read from the static area at every call (collectRuntimeConstructs keeps
                    // such an argument to calls whose arguments are all in registers).
                    arg.vreg = nextVReg++;
                    loadStaticConst(srcArgs[ai], arg.vreg);
                } else if (srcArgs[ai] <= 0) {
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
            const bool selfTail = getFuncExtraRuntimeGraph(&bc) == unit.runtimeGraph;
            if (!selfTail && !framed)
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
            if (framed) {
                // A tail call of another graph is the driver's (it replaces the frame). So is
                // a self tail call that would stage an object constant on the machine stack.
                bool viaDriver = !selfTail;
                if (moved.size() > kMaxRegisterArgs) {
                    for (const size_t i : moved)
                        viaDriver = viaDriver || isObjectConstant(srcArgs[i]);
                }
                if (viaDriver) {
                    build.emitExitToDriver(static_cast<uint32_t>(pc), x64::JitExitKind::Tail);
                    slotCache.clear();
                    break;
                }
                // The jump below is a loop without a call: the interpreter polls the collector
                // at every TAIL, and so does this code, by leaving to the driver when the
                // collector waits (the driver then makes the tail call itself).
                if (unit.safepointFlagAddr != nullptr) {
                    build.emitSafepointPoll(
                        static_cast<uint32_t>(pc),
                        reinterpret_cast<uint64_t>(unit.safepointFlagAddr));
                    slotCache.clear();
                }
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
        // The three events of a loop of regions (framed code). The frame carries the state, the
        // ports and the round counter, so the helpers of loop_frame.h run the round exactly as
        // the interpreters do; the code then jumps on what the event returned: LTST to its LEND
        // when the loop stops, LEND back to the first instruction of a round when another one
        // follows. Both events are barriers for the slot cache (the helpers write the frame).
        //
        // Where the round's shape allows it (LoopRound, H55), the two events a round has are
        // lowered instead: the test is a load and a conditional jump, the back edge a direct
        // jump, the state binding slot copies, and the round counter is not counted at all (no
        // instruction of such a round reads it; it becomes -1 on the stop path, which is what
        // every back end leaves there).
        case OpCode::LBEG: {
            const camel::runtime::ExecLoop *loop = bc.extra()->loop;
            build.emitCallHelper(static_cast<uint32_t>(pc), helperLoopBegin);
            slotCache.clear();
            const LoopRound *round = nativeRound(loop);
            if (round != nullptr) {
                // A round starts at the instruction after the LBEG, and that is where the back
                // edge goes: binding the condition's ports there binds them at the start of
                // every round, the first one included (loopBegin bound it already, and a second
                // copy of the same state costs nothing next to the loop).
                build.setNextPc(static_cast<uint32_t>(round->roundPc));
                bindLoopPorts(*loop, loop->stateCount);
            }
            break;
        }
        case OpCode::LTST: {
            const camel::runtime::ExecLoop *loop = bc.extra()->loop;
            const LoopRound *round               = nativeRound(loop);
            if (round == nullptr) {
                build.emitCallHelper(static_cast<uint32_t>(pc), helperLoopTest);
                x64::VRegId vStop = nextVReg++;
                build.emitVMovFromRax(vStop);
                build.emitVTest(vStop);
                build.emitJneRel32(static_cast<uint32_t>(getJumpTargetPc(&bc)));
                slotCache.clear();
                break;
            }
            // The test: the condition's result, false meaning the loop stops (loopTest). The
            // ports of the body take the state only on the path that goes on.
            x64::VRegId vCond = nextVReg++;
            loadSlot(loop->condResultSlot, slotDisp(loop->condResultSlot), vCond);
            build.emitVTest(vCond);
            build.emitJeRel32(round->stopPc);
            bindLoopPorts(*loop, loop->stateCount * 2);
            break;
        }
        case OpCode::LEND: {
            const camel::runtime::ExecLoop *loop = bc.extra()->loop;
            const LoopRound *round               = nativeRound(loop);
            if (round == nullptr) {
                build.emitCallHelper(static_cast<uint32_t>(pc), helperLoopEnd);
                x64::VRegId vContinue = nextVReg++;
                build.emitVMovFromRax(vContinue);
                build.emitVTest(vContinue);
                build.emitJneRel32(static_cast<uint32_t>(getJumpTargetPc(&bc)));
                slotCache.clear();
                break;
            }
            // The body fell through to here: the next round starts at the head of a round.
            build.emitJmpRel32(static_cast<uint32_t>(round->roundPc));
            // The stop path, which the test jumps to: the round count becomes -1, as loopTest
            // leaves it, and the loop ends through the same helper as every other shape — it
            // brings the loop's value tuple up to date from the components (loopEnd), once per
            // loop and not once per round. What it would do on the path that goes on is what the
            // copies above and the jump back do here.
            build.setNextPc(round->stopPc);
            x64::VRegId vStopped = nextVReg++;
            build.emitVLoadImm64(vStopped, static_cast<uint64_t>(-1));
            build.emitVStoreToFrame(slotDisp(loop->roundSlot), vStopped);
            build.emitCallHelper(static_cast<uint32_t>(pc), helperLoopEnd);
            slotCache.clear();
            build.emitJmpRel32(static_cast<uint32_t>(pc + bc.opsize));
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
        // The bytecodes below occur in framed code only (collectRuntimeConstructs).
        case OpCode::OPER:
            // The helper calls the operator on the frame and stores its result there.
            build.emitCallHelper(static_cast<uint32_t>(pc), helperOper);
            slotCache.clear();
            break;
        case OpCode::CAST:
        case OpCode::COPY:
        case OpCode::FILL:
        case OpCode::ACCS:
            build.emitCallHelper(static_cast<uint32_t>(pc), helperBytecode);
            slotCache.clear();
            break;
        case OpCode::IMOD:
        case OpCode::LMOD:
            // The remainder of an integer division, in the helper: it faults on a zero divisor
            // (the fault path needs the driver), and it stores its own result.
            build.emitCallHelper(static_cast<uint32_t>(pc), helperBytecode);
            slotCache.clear();
            break;
        case OpCode::CALL:
        case OpCode::SCHD:
            // A call through a function value, and a higher-order operator or the fault
            // isolation node: they call Camel functions, so the driver runs them.
            build.emitExitToDriver(static_cast<uint32_t>(pc), x64::JitExitKind::Interp);
            slotCache.clear();
            break;
        default:
            return fail("pc=" + std::to_string(pc) + " unsupported opcode " + to_string(bc.opcode));
        }
        // Every bytecode has an instruction that carries its pc (see MirBuilder::emitNop).
        if (mirBuf.size() == mirCountBefore)
            build.emitNop();

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
        code.insert(code.end(), {0x41, 0x57}); // push r15
        // mov rdi, rcx  (Win64 arg1 → JIT slot base)
        code.insert(code.end(), {0x48, 0x89, 0xCF});
        // mov rsi, rdx  (Win64 arg2 → JIT ctx)
        code.insert(code.end(), {0x48, 0x89, 0xD6});
        // mov r15, [rsi + depthBudget]  — calls the body may still nest
        code.insert(
            code.end(),
            {0x4C, 0x8B, 0x7E, static_cast<uint8_t>(x64::kJitCtxDepthBudgetOffset)});
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
        if (framed) {
            // mov rax, [rsi + resumeAddr]; test rax, rax; jz +4; call rax; jmp +5
            code.insert(
                code.end(),
                {0x48, 0x8B, 0x46, static_cast<uint8_t>(x64::kJitCtxResumeAddrOffset)});
            code.insert(code.end(), {0x48, 0x85, 0xC0});
            code.insert(code.end(), {0x74, 0x04});
            code.insert(code.end(), {0xFF, 0xD0});
            code.insert(code.end(), {0xEB, 0x05});
        }
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
        code.insert(code.end(), {0x41, 0x57});       // push r15
        code.insert(code.end(), {0x49, 0x89, 0xF5}); // mov r13, rsi
        // mov r15, [rsi + depthBudget]  — calls the body may still nest
        code.insert(
            code.end(),
            {0x4C, 0x8B, 0x7E, static_cast<uint8_t>(x64::kJitCtxDepthBudgetOffset)});
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
        if (framed) {
            // mov rax, [rsi + resumeAddr]; test rax, rax; jz +4; call rax; jmp +5
            code.insert(
                code.end(),
                {0x48, 0x8B, 0x46, static_cast<uint8_t>(x64::kJitCtxResumeAddrOffset)});
            code.insert(code.end(), {0x48, 0x85, 0xC0});
            code.insert(code.end(), {0x74, 0x04});
            code.insert(code.end(), {0xFF, 0xD0});
            code.insert(code.end(), {0xEB, 0x05});
        }
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
