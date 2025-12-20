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
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "compile/gir.h"
#include "utils/rawarr.h"

// 分布密集的字节码指令集
// 用于加速 switch 分派，降低 CPU 分支预测失败率
enum class OpCode : uint8_t {
    // 定长参数指令
    RETN,
    CAST,
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

    // 常用算子快捷指令（定长）
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

inline std::string formatIndex(data_idx_t value) {
    int width = 0;
    if (value == 0) {
        width = 1;
    } else {
        int temp = value < 0 ? -value : value;
        while (temp > 0) {
            temp /= 10;
            width++;
        }
        if (value < 0) {
            width++; // 负号占一位
        }
    }
    width = (width + 1) / 2 * 2;
    return std::format("{:>{}}", value, width);
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

    inline BytecodeExtra *extra() { return reinterpret_cast<BytecodeExtra *>(this + opsize - 1); }

    inline const BytecodeExtra *extra() const {
        return reinterpret_cast<const BytecodeExtra *>(this + opsize - 1);
    }
};

using Bytecode = BytecodeHeader;

union BytecodeExtra {      // 8 bytes
    Type *pType;           // for CAST
    GraphIR::Graph *graph; // for FUNC
    operator_t func;       // for OPER
    MarkOpCode mark;       // for SCHD

    std::string toString(OpCode opcode) const;
};

static_assert(sizeof(Bytecode) == 8, "Bytecode must be exactly 8 bytes");
static_assert(sizeof(BytecodeHeader) == 8, "BytecodeHeader must be exactly 8 bytes");
static_assert(sizeof(BytecodeExtra) == 8, "BytecodeExtra must be exactly 8 bytes");

using bytecode_vec_t = std::vector<Bytecode>;

inline size_t roundUp8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

Bytecode *appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, data_idx_t result,
    const std::vector<data_idx_t> &fastops = {}, const std::vector<data_idx_t> &normOperands = {},
    const std::vector<data_idx_t> &withOperands = {}, bool hasExtra = false,
    const BytecodeExtra &extra = {});