/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Oct. 21, 2025
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include "camel/utils/assert.h"

namespace type = camel::core::type;

namespace camel::runtime {
class GCGraph;
struct GCFillBody;
} // namespace camel::runtime

// Densely packed bytecode instruction set.
// Used to speed up switch dispatch and reduce CPU branch misprediction.
enum class OpCode : uint8_t {
    // Fixed-arity instructions
    RETN,
    CAST, // Use fastop[0] as the slot index to cast; extra stores the target type.
    COPY, // Use fastop[0] as the source index to copy.
    ACCS, // fastop[0] is the target index; fastop[1] is the accessed subscript.
    JUMP, // Use fastop[0] as the jump target address.

    // Variable-arity instructions
    BRCH,
    JOIN,
    FILL,
    CALL,
    FUNC,
    TAIL, // Mark tail calls.
    OPER,
    SCHD, // Use fastop[0] as the scheduling strategy ID.

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
    MapArr,
    ApplyArr,
    ReduceArr,
    FilterArr,
    ForeachArr,
};

// 0 means null; positive values are dynamic data indices, negative values are the negated static
// data indices.
using data_idx_t = int16_t;
using arr_size_t = uint16_t;

using data_arr_t = std::span<const data_idx_t>;

inline bool isSafeSizeTForIndexT(size_t value) {
    return value <= static_cast<size_t>(std::numeric_limits<data_idx_t>::max());
}

template <typename T> inline data_idx_t as_index(T value) {
    if constexpr (std::is_same_v<T, size_t>) {
        ASSERT(
            isSafeSizeTForIndexT(value),
            "Value exceeds int16_t range when converting to data_idx_t.");
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

// Bytecode layout (Total size = opsize * 8 bytes)
//
// +-----------------------------+
// | BytecodeHeader (8 bytes)   |
// |-----------------------------|
// | opcode     : 1 byte         |
// | opsize     : 1 byte         |
// | result     : 2 bytes        |
// | fastop[0]  : 2 bytes        |
// | fastop[1]  : 2 bytes        |
// +-----------------------------+
//
// +-----------------------------+
// | BytecodeOperands (optional) |
// |-----------------------------|
// | operands[] : 2 bytes each   |
// | total bytes padded to 8B    |
// +-----------------------------+
//
// +-----------------------------+
// | BytecodeExtra (optional, 8B)|
// |-----------------------------|
// | union value : 8 bytes       |
// +-----------------------------+
//
// total_size = opsize * 8 bytes

struct BytecodeHeader {                  // 8 bytes
    OpCode opcode        = OpCode::RETN; // 1 byte
    uint8_t opsize       = 0;            // 1 byte, measured in 8-byte units.
    data_idx_t result    = 0;            // 2 bytes
    data_idx_t fastop[2] = {0, 0};       // 4 bytes

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
        case OpCode::OPER:
            [[fallthrough]];
        case OpCode::SCHD:
            return 1;
        case OpCode::FILL:
            return 2;
        case OpCode::FUNC:
            [[fallthrough]];
        case OpCode::TAIL:
#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
            return 3;
#else
            return 2;
#endif
        default:
            return 0;
        }
    }

    bool hasExtraWord() const { return extraWordCount() != 0; }

    inline BytecodeExtra *extra() {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords != 0, "Bytecode has no extra payload.");
        return reinterpret_cast<BytecodeExtra *>(this + opsize - extraWords);
    }

    inline const BytecodeExtra *extra() const {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords != 0, "Bytecode has no extra payload.");
        return reinterpret_cast<const BytecodeExtra *>(this + opsize - extraWords);
    }

    // FILL uses two extra words. FUNC/TAIL use two extra words in non-JIT builds and three extra
    // words in JIT builds. extra() returns the first word.
    inline uint64_t *extra2() {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 2, "Bytecode has no second extra payload word.");
        return reinterpret_cast<uint64_t *>(this + opsize - extraWords + 1);
    }
    inline const uint64_t *extra2() const {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 2, "Bytecode has no second extra payload word.");
        return reinterpret_cast<const uint64_t *>(this + opsize - extraWords + 1);
    }

#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
    inline uint64_t *extra3() {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 3, "Bytecode has no third extra payload word.");
        return reinterpret_cast<uint64_t *>(this + opsize - extraWords + 2);
    }
    inline const uint64_t *extra3() const {
        const size_t extraWords = extraWordCount();
        ASSERT(extraWords >= 3, "Bytecode has no third extra payload word.");
        return reinterpret_cast<const uint64_t *>(this + opsize - extraWords + 2);
    }
#endif
};

using Bytecode = BytecodeHeader;

union BytecodeExtra {                           // 8 bytes
    type::Type *pType;                          // for CAST
    camel::runtime::GCGraph *runtimeGraph;      // runtime FUNC/TAIL target
    const camel::runtime::GCFillBody *fillBody; // FILL slot mapping payload
    operator_t func;                            // for OPER
    MarkOpCode mark;                            // for SCHD
    uint64_t raw;                               // generic

    std::string toString(OpCode opcode) const;
};

// FUNC/TAIL:
// - extra()  : runtime callee graph pointer
// - extra2() : [targetPc:16 | directCallCount:48]
// - extra3() : JIT entry pointer (JIT builds only; 0 means interpreter entry)
inline camel::runtime::GCGraph *getFuncExtraRuntimeGraph(const BytecodeHeader *bc) {
    return bc->extra()->runtimeGraph;
}
inline void setFuncExtraRuntimeGraph(BytecodeHeader *bc, camel::runtime::GCGraph *graph) {
    bc->extra()->runtimeGraph = graph;
}

constexpr uint64_t kFuncExtraCountMask     = (1ull << 48) - 1;
constexpr uint64_t kFuncExtraTargetPcShift = 48;
constexpr uint64_t kFuncExtraTargetPcMask  = 0xFFFFull;

inline uint32_t getFuncExtraCount(const BytecodeHeader *bc) {
    const uint64_t count = *bc->extra2() & kFuncExtraCountMask;
    return count >= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
               ? std::numeric_limits<uint32_t>::max()
               : static_cast<uint32_t>(count);
}
inline size_t getFuncExtraTargetPc(const BytecodeHeader *bc) {
    return static_cast<size_t>((*bc->extra2() >> kFuncExtraTargetPcShift) & kFuncExtraTargetPcMask);
}
inline void setFuncExtraTargetPc(BytecodeHeader *bc, size_t targetPc) {
    ASSERT(targetPc <= kFuncExtraTargetPcMask, "JIT target pc exceeds packed FuncExtra range.");
    const uint64_t count = *bc->extra2() & kFuncExtraCountMask;
    *bc->extra2()        = (static_cast<uint64_t>(targetPc) << kFuncExtraTargetPcShift) | count;
}
inline uint32_t incFuncExtraCount(BytecodeHeader *bc) {
    const uint64_t targetPc = *bc->extra2() & ~kFuncExtraCountMask;
    uint64_t count          = *bc->extra2() & kFuncExtraCountMask;
    if (count < static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        ++count;
    }
    *bc->extra2() = targetPc | count;
    return static_cast<uint32_t>(count);
}

#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
inline void *getFuncExtraFn(BytecodeHeader *bc) { return reinterpret_cast<void *>(*bc->extra3()); }
inline void *getFuncExtraFn(const BytecodeHeader *bc) {
    return reinterpret_cast<void *>(*bc->extra3());
}
inline void setFuncExtraFn(BytecodeHeader *bc, void *fn) {
    *bc->extra3() = reinterpret_cast<uint64_t>(fn);
}
#endif

static_assert(sizeof(Bytecode) == 8, "Bytecode must be exactly 8 bytes");
static_assert(sizeof(BytecodeHeader) == 8, "BytecodeHeader must be exactly 8 bytes");
static_assert(sizeof(BytecodeExtra) == 8, "BytecodeExtra must be exactly 8 bytes");

using bytecode_vec_t = std::vector<Bytecode>;

inline size_t roundUp8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

Bytecode *appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, data_idx_t result,
    const std::vector<data_idx_t> &fastops = {}, const std::vector<data_idx_t> &normOperands = {},
    const std::vector<data_idx_t> &withOperands = {}, bool hasExtra = false,
    const BytecodeExtra &extra = {}, size_t extraUnits = 1);
