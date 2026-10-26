// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <stdexcept>

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include "camel/utils/assert.h"

namespace type = camel::core::type;

namespace camel::runtime {
class GCGraph;
struct GCFillBody;
struct ExecLoop;
} // namespace camel::runtime

// Densely packed bytecode instruction set.
// Used to speed up switch dispatch and reduce CPU branch misprediction.
enum class OpCode : uint8_t {
    // Fixed-arity instructions
    RETN,
    CAST, // Use fastop[0] as the slot index to cast; extra stores the target type.
    COPY, // Use fastop[0] as the source index to copy.
    ACCS, // fastop[0] is the target index; fastop[1] is the accessed subscript.
    JUMP, // The jump target pc: see getJumpTargetPc (fastop[0], and fastop[1] once linked).

    // Variable-arity instructions
    BRCH,
    JOIN,
    FILL,
    CALL,
    FUNC,
    TAIL, // Mark tail calls.
    OPER,
    SCHD, // Use fastop[0] as the scheduling strategy ID.

    // The three events of a loop that is a region of the graph (camel/runtime/exec_plan.h,
    // ExecLoop; camel/execute/loop_frame.h). The extra word is the ExecLoop. LBEG starts the loop;
    // LTST runs after the condition and jumps to the pc in fastop[0] (the LEND) when the loop
    // stops; LEND runs after the body and jumps to the pc in fastop[0] (the first instruction of
    // the round) when another round follows. Like a JUMP, the target is graph-relative until the
    // linker makes it absolute (getJumpTargetPc).
    LBEG,
    LTST,
    LEND,

    // Common arithmetic shortcuts (fixed arity): binary arithmetic/comparison; both operands are
    // slot indices.
    // fastop[0] and fastop[1]: >0 means a Frame slot, <0 means a static-area slot; result is the
    // destination slot.
    // Arithmetic: result = fastop[0] op fastop[1]; comparison: result = (fastop[0] op fastop[1]) ?
    // 1 : 0
    IADD,
    LADD,
    FADD,
    DADD,

    ISUB,
    LSUB,
    FSUB,
    DSUB,

    IMUL,
    LMUL,
    FMUL,
    DMUL,

    IDIV,
    LDIV,
    FDIV,
    DDIV,

    // The remainder of an integer division, as the operator `%` states it (camel/utils/int_div.h:
    // x % -1 is 0, and a zero divisor is a fault).
    IMOD,
    LMOD,

    ILT,
    LLT,
    FLT,
    DLT,

    IGT,
    LGT,
    FGT,
    DGT,

    IEQ,
    LEQ,
    FEQ,
    DEQ,

    INE,
    LNE,
    FNE,
    DNE,

    ILE,
    LLE,
    FLE,
    DLE,

    IGE,
    LGE,
    FGE,
    DGE,
};

/// The instructions that jump to the pc in their operands (getJumpTargetPc).
inline bool hasJumpTarget(OpCode opcode) {
    return opcode == OpCode::JUMP || opcode == OpCode::LTST || opcode == OpCode::LEND;
}

inline bool hasDynamicOperands(OpCode opcode) {
    // BRCH .. SCHD: variable-arity instructions
    switch (opcode) {
    case OpCode::BRCH:
        [[fallthrough]];
    case OpCode::JOIN:
        [[fallthrough]];
    case OpCode::FILL:
        [[fallthrough]];
    case OpCode::CALL:
        [[fallthrough]];
    case OpCode::FUNC:
        [[fallthrough]];
    case OpCode::TAIL:
        [[fallthrough]];
    case OpCode::OPER:
        [[fallthrough]];
    case OpCode::SCHD:
        return true;
    default:
        return false;
    }
}

inline bool isDirectCallOpcode(OpCode opcode) {
    return opcode == OpCode::FUNC || opcode == OpCode::TAIL;
}

enum class MarkOpCode {
    MapArr, // also `apply`, which returns a new array as `map` does
    ReduceArr,
    FilterArr,
    ForeachArr,
    Isolate, // the fault-isolation node (camel/execute/fault_isolation.h)
};

// 0 means null; positive values are dynamic data indices, negative values are the negated static
// data indices.
using data_idx_t = int32_t;

using data_arr_t = std::span<const data_idx_t>;

inline bool isSafeSizeTForIndexT(size_t value) {
    return value <= static_cast<size_t>(std::numeric_limits<data_idx_t>::max());
}

template <typename T> inline data_idx_t as_index(T value) {
    if constexpr (std::is_same_v<T, size_t>) {
        // Checked in every build: a truncated slot index or jump target runs on other data.
        if (!isSafeSizeTForIndexT(value)) {
            throw std::runtime_error(
                std::format(
                    "FastVM: index {} does not fit the 32-bit operand of a bytecode",
                    value));
        }
    } else {
        static_assert(false, "Unsupported type for data_idx_t conversion.");
    }
    return static_cast<data_idx_t>(value);
}

inline int computeWidth(data_idx_t value) {
    int width = 0;
    if (value == 0) {
        width = 1;
    } else {
        int temp = value < 0 ? -value : value;
        while (temp > 0) {
            temp /= 10;
            width++;
        }
    }
    return width;
}

inline std::string formatIndex(int value, int maxwidth) {
    int width = computeWidth(value);

    if (width < maxwidth)
        return std::format("{:>{}}", value, maxwidth);
    else
        return std::format("{}", value);
}

std::string to_string(const OpCode &op);
std::string to_string(const MarkOpCode &op);

struct BytecodeHeader;
union BytecodeExtra;

// Bytecode layout (Total size = opsize * 16 bytes)
//
// +-----------------------------+
// | BytecodeHeader (16 bytes)   |
// |-----------------------------|
// | opcode     : 1 byte         |
// | reserved   : 1 byte         |
// | opsize     : 2 bytes        |
// | result     : 4 bytes        |
// | fastop[0]  : 4 bytes        |
// | fastop[1]  : 4 bytes        |
// +-----------------------------+
//
// +-----------------------------+
// | BytecodeOperands (optional) |
// |-----------------------------|
// | operands[] : 4 bytes each   |
// | total bytes padded to 16B   |
// +-----------------------------+
//
// +-----------------------------+
// | BytecodeExtra (optional)    |
// |-----------------------------|
// | 8-byte words, ending where  |
// | the instruction ends        |
// +-----------------------------+
//
// An instruction takes opsize units of 16 bytes (sizeof(Bytecode)); a pc counts units.

struct BytecodeHeader {                  // 16 bytes
    OpCode opcode        = OpCode::RETN; // 1 byte
    uint8_t reserved0    = 0;            // 1 byte
    uint16_t opsize      = 0;            // 2 bytes, measured in 16-byte units.
    data_idx_t result    = 0;            // 4 bytes
    data_idx_t fastop[2] = {0, 0};       // 8 bytes

    std::string toString() const;

    size_t normCnt() const { return static_cast<size_t>(fastop[0]); }
    size_t withCnt() const { return static_cast<size_t>(fastop[1]); }
    size_t argsCnt() const { return normCnt() + withCnt(); }

    size_t directCallArgCnt() const {
        ASSERT(isDirectCallOpcode(opcode), "Direct-call arg count requires FUNC/TAIL bytecode.");
        return normCnt();
    }

    const data_arr_t nargs() const {
        return data_arr_t{reinterpret_cast<const data_idx_t *>(this + 1), normCnt()};
    }
    const data_arr_t wargs() const {
        return data_arr_t{reinterpret_cast<const data_idx_t *>(this + 1) + normCnt(), withCnt()};
    }

    const data_arr_t directCallSrcArgs() const {
        ASSERT(isDirectCallOpcode(opcode), "Direct-call source args require FUNC/TAIL bytecode.");
        return nargs();
    }
    const data_arr_t directCallDstSlots() const {
        ASSERT(
            isDirectCallOpcode(opcode),
            "Direct-call destination slots require FUNC/TAIL bytecode.");
        return wargs();
    }

    inline data_idx_t *operands() { return reinterpret_cast<data_idx_t *>(this + 1); }

    inline const data_idx_t *operands() const {
        return reinterpret_cast<const data_idx_t *>(this + 1);
    }

    size_t extraWordCount() const {
        switch (opcode) {
        case OpCode::CAST:
            [[fallthrough]];
        case OpCode::JOIN:
            [[fallthrough]];
        case OpCode::LBEG:
            [[fallthrough]];
        case OpCode::LTST:
            [[fallthrough]];
        case OpCode::LEND:
            [[fallthrough]];
        case OpCode::SCHD:
            return 1;
        case OpCode::OPER: // Native function pointer, its OperId, and the node's attribute set.
            return 3;
        case OpCode::FILL:
            return 2;
        case OpCode::FUNC:
            [[fallthrough]];
        case OpCode::TAIL:
            // The runtime callee graph, [targetPc | callCount], the JIT entry pointer, and the
            // written-capture mask. The count must equal directCallExtraWordCount().
            return 4;
        default:
            return 0;
        }
    }

    bool hasExtraWord() const { return extraWordCount() != 0; }

    /// The loop an LBEG, LTST or LEND belongs to (their one extra word). The handlers know these
    /// three carry exactly that word, which extra() would work out from the opcode again; a round
    /// asks for it at each of its two events.
    inline const camel::runtime::ExecLoop *loopPayload() const;

    inline BytecodeExtra *extra() {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords != 0, "Bytecode has no extra payload.");
        return reinterpret_cast<BytecodeExtra *>(reinterpret_cast<uint64_t *>(this + opsize) - extraWords);
    }

    inline const BytecodeExtra *extra() const {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords != 0, "Bytecode has no extra payload.");
        return reinterpret_cast<const BytecodeExtra *>(
            reinterpret_cast<const uint64_t *>(this + opsize) - extraWords);
    }

    // FILL uses two extra words. OPER uses three: its function pointer, its OperId, and the
    // attribute set of the node it runs (camel/core/operator.h, ArgsView::attrs). FUNC/TAIL use
    // four, in every build. extra() returns the first word.
    inline uint64_t *extra2() {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 2, "Bytecode has no second extra payload word.");
        return reinterpret_cast<uint64_t *>(this + opsize) - extraWords + 1;
    }
    inline const uint64_t *extra2() const {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 2, "Bytecode has no second extra payload word.");
        return reinterpret_cast<const uint64_t *>(this + opsize) - extraWords + 1;
    }

#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
    inline uint64_t *extra3() {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 3, "Bytecode has no third extra payload word.");
        return reinterpret_cast<uint64_t *>(this + opsize) - extraWords + 2;
    }
    inline const uint64_t *extra3() const {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 3, "Bytecode has no third extra payload word.");
        return reinterpret_cast<const uint64_t *>(this + opsize) - extraWords + 2;
    }
#endif

    /// OPER's third extra word: the attribute set of the node it runs (camel/core/node_attrs.h).
    inline camel::core::attr::AttrSetId *operAttrs() {
        ASSERT(opcode == OpCode::OPER, "Bytecode is not an OPER.");
        return reinterpret_cast<camel::core::attr::AttrSetId *>(extra2() + 1);
    }
    inline const camel::core::attr::AttrSetId *operAttrs() const {
        ASSERT(opcode == OpCode::OPER, "Bytecode is not an OPER.");
        return reinterpret_cast<const camel::core::attr::AttrSetId *>(extra2() + 1);
    }

    // FUNC/TAIL use four extra words, whether or not this build has a JIT: the runtime callee
    // graph, [targetPc:32 | callCount:32], the JIT entry pointer (0 without a JIT) and the
    // written-capture mask. It must equal the FUNC/TAIL case of extraWordCount().
    static constexpr size_t directCallExtraWordCount() { return 4; }

    // FUNC/TAIL are the tightest recursive-call path. Their payload layout is fixed by
    // appendBytecode(..., extraUnits), so use constant-offset accessors instead of extra(), whose
    // generic opcode switch is measurable when fib-like code calls through this on every frame.
    inline BytecodeExtra *directCallExtra() {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<BytecodeExtra *>(
            reinterpret_cast<uint64_t *>(this + opsize) - directCallExtraWordCount());
    }
    inline const BytecodeExtra *directCallExtra() const {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<const BytecodeExtra *>(
            reinterpret_cast<const uint64_t *>(this + opsize) - directCallExtraWordCount());
    }
    inline uint64_t *directCallExtra2() {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<uint64_t *>(this + opsize) - directCallExtraWordCount() + 1;
    }
    inline const uint64_t *directCallExtra2() const {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<const uint64_t *>(this + opsize) - directCallExtraWordCount() + 1;
    }
#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
    inline uint64_t *directCallExtra3() {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<uint64_t *>(this + opsize) - directCallExtraWordCount() + 2;
    }
    inline const uint64_t *directCallExtra3() const {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<const uint64_t *>(this + opsize) - directCallExtraWordCount() + 2;
    }
#endif
    inline uint64_t *directCallExtraWriteMask() {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<uint64_t *>(this + opsize) - directCallExtraWordCount() + 3;
    }
    inline const uint64_t *directCallExtraWriteMask() const {
        ASSERT(isDirectCallOpcode(opcode), "Bytecode is not a direct call.");
        return reinterpret_cast<const uint64_t *>(this + opsize) - directCallExtraWordCount() + 3;
    }
};

using Bytecode = BytecodeHeader;

union BytecodeExtra {                           // 8 bytes
    type::Type *pType;                          // for CAST
    camel::runtime::GCGraph *runtimeGraph;      // runtime FUNC/TAIL target
    const camel::runtime::GCFillBody *fillBody; // FILL slot mapping payload
    const camel::runtime::ExecLoop *loop;       // LBEG, LTST, LEND
    operator_t func;                            // for OPER
    MarkOpCode mark;                            // for SCHD
    uint64_t raw; // generic

    std::string toString(OpCode opcode) const;
};

inline const camel::runtime::ExecLoop *BytecodeHeader::loopPayload() const {
    ASSERT(
        opcode == OpCode::LBEG || opcode == OpCode::LTST || opcode == OpCode::LEND,
        "Bytecode is not a loop event.");
    return reinterpret_cast<const BytecodeExtra *>(reinterpret_cast<const uint64_t *>(this + opsize) - 1)
        ->loop;
}

// FUNC/TAIL:
// - extra()  : runtime callee graph pointer
// - extra2() : [targetPc:32 | directCallCount:32]
// - extra3() : JIT entry pointer (JIT builds only; 0 means interpreter entry)
// - extra4() : which captured variables the callee writes, as a bitmask over its capture ports,
//              which is what the call hands on the new version of beside its return value
inline camel::runtime::GCGraph *getFuncExtraRuntimeGraph(const BytecodeHeader *bc) {
    return bc->directCallExtra()->runtimeGraph;
}
inline void setFuncExtraRuntimeGraph(BytecodeHeader *bc, camel::runtime::GCGraph *graph) {
    bc->directCallExtra()->runtimeGraph = graph;
}

// The captured variables the callee writes: bit k is the callee's k-th capture, in capture-table
// order. The call's value is a tuple of its return value and the new version of each of them, so
// the mask says both which with-inputs are handed the versions and how many items the call's
// value has. A callee that writes none has 0 and its call's value is the return value alone.
inline uint32_t getFuncExtraWriteMask(const BytecodeHeader *bc) {
    return static_cast<uint32_t>(*bc->directCallExtraWriteMask());
}
inline void setFuncExtraWriteMask(BytecodeHeader *bc, uint32_t writeMask) {
    *bc->directCallExtraWriteMask() = writeMask;
}

// JUMP: the target pc. Inside one graph, before linking, it is graph-relative and fits fastop[0]
// (fastop[1] is 0). The linker makes it absolute, and a linked program can exceed 32 bits of
// operand: the absolute target is spread over both operands, low 32 bits in fastop[0], high 32
// bits in fastop[1]. Every reader of a linked JUMP goes through getJumpTargetPc.
inline size_t getJumpTargetPc(const BytecodeHeader *bc) {
    return static_cast<size_t>(static_cast<uint32_t>(bc->fastop[0])) |
           (static_cast<size_t>(static_cast<uint32_t>(bc->fastop[1])) << 32);
}
inline void setJumpTargetPc(BytecodeHeader *bc, size_t targetPc) {
    bc->fastop[0] = static_cast<data_idx_t>(static_cast<uint32_t>(targetPc & 0xFFFFFFFFull));
    bc->fastop[1] = static_cast<data_idx_t>(static_cast<uint32_t>(targetPc >> 32));
}

constexpr uint64_t kFuncExtraCountMask     = (1ull << 32) - 1;
constexpr uint64_t kFuncExtraTargetPcShift = 32;
constexpr uint64_t kFuncExtraTargetPcMask  = 0xFFFFFFFFull;

inline uint32_t getFuncExtraCount(const BytecodeHeader *bc) {
    const uint64_t count = *bc->directCallExtra2() & kFuncExtraCountMask;
    return count >= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
               ? std::numeric_limits<uint32_t>::max()
               : static_cast<uint32_t>(count);
}
inline size_t getFuncExtraTargetPc(const BytecodeHeader *bc) {
    return static_cast<size_t>(
        (*bc->directCallExtra2() >> kFuncExtraTargetPcShift) & kFuncExtraTargetPcMask);
}
inline void setFuncExtraTargetPc(BytecodeHeader *bc, size_t targetPc) {
    if (targetPc > kFuncExtraTargetPcMask) {
        // Checked in every build: a truncated target would run other code.
        throw std::runtime_error(
            std::format(
                "FastVM: call target pc {} exceeds the {} bytecode words a program may have",
                targetPc,
                kFuncExtraTargetPcMask));
    }
    const uint64_t count    = *bc->directCallExtra2() & kFuncExtraCountMask;
    *bc->directCallExtra2() = (static_cast<uint64_t>(targetPc) << kFuncExtraTargetPcShift) | count;
}
inline uint32_t incFuncExtraCount(BytecodeHeader *bc) {
    const uint64_t targetPc = *bc->directCallExtra2() & ~kFuncExtraCountMask;
    uint64_t count          = *bc->directCallExtra2() & kFuncExtraCountMask;
    if (count < static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        ++count;
    }
    *bc->directCallExtra2() = targetPc | count;
    return static_cast<uint32_t>(count);
}

#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
inline void *getFuncExtraFn(BytecodeHeader *bc) {
    return reinterpret_cast<void *>(*bc->directCallExtra3());
}
inline void *getFuncExtraFn(const BytecodeHeader *bc) {
    return reinterpret_cast<void *>(*bc->directCallExtra3());
}
inline void setFuncExtraFn(BytecodeHeader *bc, void *fn) {
    *bc->directCallExtra3() = reinterpret_cast<uint64_t>(fn);
}
#endif

static_assert(sizeof(Bytecode) == 16, "Bytecode must be exactly 16 bytes");
static_assert(sizeof(BytecodeHeader) == 16, "BytecodeHeader must be exactly 16 bytes");
static_assert(sizeof(BytecodeExtra) == 8, "BytecodeExtra must be exactly 8 bytes");

using bytecode_vec_t = std::vector<Bytecode>;

inline size_t roundUp16(size_t n) { return (n + 15) & ~static_cast<size_t>(15); }

Bytecode *appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, data_idx_t result,
    const std::vector<data_idx_t> &fastops = {}, const std::vector<data_idx_t> &normOperands = {},
    const std::vector<data_idx_t> &withOperands = {}, bool hasExtra = false,
    const BytecodeExtra &extra = {}, size_t extraWords = 1);
