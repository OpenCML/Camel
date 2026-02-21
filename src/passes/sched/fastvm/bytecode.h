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
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "camel/compile/gir.h"
#include "camel/utils/rawarr.h"

// 分布密集的字节码指令集
// 用于加速 switch 分派，降低 CPU 分支预测失败率
enum class OpCode : uint8_t {
    // 定长参数指令
    RETN,
    CAST, // 使用 fastop[0] 作为待转换的 slot 索引，extra 存储目标类型
    COPY, // 使用 fastop[0] 作为待拷贝索引
    ACCS, // fastop[0] 作为目标索引，fastop[1] 作为访问的下标
    JUMP, // 使用 fastop[0] 作为跳转目标地址

    // 变长参数指令
    BRCH,
    JOIN,
    FILL,
    CALL,
    FUNC,
    TAIL, // 标记尾调用
    OPER,
    SCHD, // 使用 fastop[0] 作为调度策略 ID

    // 常用算子快捷指令（定长）：二元运算/比较，两个操作数均为 slot 索引
    // fastop[0]、fastop[1]：>0 表示 Frame 槽，<0 表示静态区；result 为结果槽
    // 算术：result = fastop[0] op fastop[1]；比较：result = (fastop[0] op fastop[1]) ? 1 : 0
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
    // BRCH .. SCHD: 变长参数指令
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

enum class MarkOpCode {
    MapArr,
    ApplyArr,
    ReduceArr,
    FilterArr,
    ForeachArr,
};

// 0 代表空，正数表示动态数据段索引，负数表示静态数据段索引的相反数
using data_idx_t = int16_t;
using arr_size_t = uint16_t;

using data_arr_t = RawArray<const data_idx_t>;

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
    uint8_t opsize       = 0;            // 1 byte，单位为 8 字节
    data_idx_t result    = 0;            // 2 bytes
    data_idx_t fastop[2] = {0, 0};       // 4 bytes

    std::string toString() const;

    size_t normCnt() const { return static_cast<size_t>(fastop[0]); }
    size_t withCnt() const { return static_cast<size_t>(fastop[1]); }
    size_t argsCnt() const { return normCnt() + withCnt(); }

    const data_arr_t nargs() const {
        return data_arr_t{reinterpret_cast<const data_idx_t *>(this + 1), normCnt()};
    }
    const data_arr_t wargs() const {
        return data_arr_t{reinterpret_cast<const data_idx_t *>(this + 1) + normCnt(), withCnt()};
    }

    inline data_idx_t *operands() { return reinterpret_cast<data_idx_t *>(this + 1); }

    inline const data_idx_t *operands() const {
        return reinterpret_cast<const data_idx_t *>(this + 1);
    }

    inline BytecodeExtra *extra() {
#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
        if (opcode == OpCode::FUNC || opcode == OpCode::TAIL)
            return reinterpret_cast<BytecodeExtra *>(this + opsize - 2);
#endif
        return reinterpret_cast<BytecodeExtra *>(this + opsize - 1);
    }

    inline const BytecodeExtra *extra() const {
#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
        if (opcode == OpCode::FUNC || opcode == OpCode::TAIL)
            return reinterpret_cast<const BytecodeExtra *>(this + opsize - 2);
#endif
        return reinterpret_cast<const BytecodeExtra *>(this + opsize - 1);
    }

#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
    // 仅对 FUNC/TAIL 有效：第二块 extra 字（count 或 JitEntryFn）
    inline uint64_t *extra2() { return reinterpret_cast<uint64_t *>(this + opsize - 1); }
    inline const uint64_t *extra2() const {
        return reinterpret_cast<const uint64_t *>(this + opsize - 1);
    }
#endif
};

using Bytecode = BytecodeHeader;

union BytecodeExtra {      // 8 bytes
    Type *pType;           // for CAST
    GraphIR::Graph *graph; // for FUNC/TAIL word0: Graph* only
    operator_t func;       // for OPER
    MarkOpCode mark;       // for SCHD
    uint64_t raw;          // generic

    std::string toString(OpCode opcode) const;
};

// FUNC/TAIL：第一块 extra 为 Graph*；启用了 JIT 时第二块 extra 为 count（未 JIT）或 JitEntryFn（已
// JIT）
inline GraphIR::Graph *getFuncExtraGraph(const BytecodeHeader *bc) { return bc->extra()->graph; }

#if defined(ENABLE_FASTVM_JIT) && ENABLE_FASTVM_JIT
inline uint32_t getFuncExtraCount(BytecodeHeader *bc) {
    return static_cast<uint32_t>(*bc->extra2());
}
inline void *getFuncExtraFn(BytecodeHeader *bc) { return reinterpret_cast<void *>(*bc->extra2()); }
inline void *getFuncExtraFn(const BytecodeHeader *bc) {
    return reinterpret_cast<void *>(*bc->extra2());
}
inline void setFuncExtraFn(BytecodeHeader *bc, void *fn) {
    *bc->extra2() = reinterpret_cast<uint64_t>(fn);
    bc->fastop[1] = 0;
}
inline uint32_t incFuncExtraCount(BytecodeHeader *bc) {
    uint64_t *p = bc->extra2();
    *p          = *p + 1;
    return static_cast<uint32_t>(*p);
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
