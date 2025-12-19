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

enum class OpCode : uint8_t {
    // 定长参数指令
    RETN = 0b00000000,
    CAST = 0b00000001,
    COPY = 0b00000010, // 使用 fastop[0] 作为待拷贝索引
    ACCS = 0b00000011, // fastop[0] 作为目标索引，fastop[1] 作为访问的下标
    JUMP = 0b00000100, // 使用 fastop[0] 作为跳转目标地址

    // 变长参数指令
    BRCH = 0b10000000,
    JOIN = 0b10000001,
    FILL = 0b10000010,
    CALL = 0b10000011,
    FUNC = 0b10000100,
    TAIL = 0b10000101, // 标记尾调用
    OPER = 0b10000110,
    SCHD = 0b10000111, // 使用 fastop[0] 作为调度策略 ID

    // 常用算子快捷指令（定长）
    IADD = 0b01000000,
    LADD = 0b01000001,
    FADD = 0b01000010,
    DADD = 0b01000011,

    ISUB = 0b01000100,
    LSUB = 0b01000101,
    FSUB = 0b01000110,
    DSUB = 0b01000111,

    IMUL = 0b01001000,
    LMUL = 0b01001001,
    FMUL = 0b01001010,
    DMUL = 0b01001011,

    IDIV = 0b01001100,
    LDIV = 0b01001101,
    FDIV = 0b01001110,
    DDIV = 0b01001111,

    ILT = 0b01010000,
    LLT = 0b01010001,
    FLT = 0b01010010,
    DLT = 0b01010011,

    IGT = 0b01010100,
    LGT = 0b01010101,
    FGT = 0b01010110,
    DGT = 0b01010111,

    IEQ = 0b01011000,
    LEQ = 0b01011001,
    FEQ = 0b01011010,
    DEQ = 0b01011011,

    INE = 0b01011100,
    LNE = 0b01011101,
    FNE = 0b01011110,
    DNE = 0b01011111,

    ILE = 0b01100000,
    LLE = 0b01100001,
    FLE = 0b01100010,
    DLE = 0b01100011,

    IGE = 0b01100100,
    LGE = 0b01100101,
    FGE = 0b01100110,
    DGE = 0b01100111,
};

// 分布密集的字节码指令集
// 用于加速 switch 分派，降低 CPU 分支预测失败率
enum class DenseOpCode : uint8_t {
    RETN,
    CAST,
    COPY,
    ACCS,
    JUMP,
    BRCH,
    JOIN,
    FILL,
    CALL,
    FUNC,
    TAIL,
    OPER,
    SCHD,
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
    COUNT // 用于统计总数
};

// 映射表：DenseOpCode → OpCode
static constexpr std::array<OpCode, static_cast<size_t>(DenseOpCode::COUNT)> DenseToOpCodeTable = {
    OpCode::RETN, OpCode::CAST, OpCode::COPY, OpCode::ACCS, OpCode::JUMP, OpCode::BRCH,
    OpCode::JOIN, OpCode::FILL, OpCode::CALL, OpCode::FUNC, OpCode::TAIL, OpCode::OPER,
    OpCode::SCHD, OpCode::IADD, OpCode::LADD, OpCode::FADD, OpCode::DADD, OpCode::ISUB,
    OpCode::LSUB, OpCode::FSUB, OpCode::DSUB, OpCode::IMUL, OpCode::LMUL, OpCode::FMUL,
    OpCode::DMUL, OpCode::IDIV, OpCode::LDIV, OpCode::FDIV, OpCode::DDIV, OpCode::ILT,
    OpCode::LLT,  OpCode::FLT,  OpCode::DLT,  OpCode::IGT,  OpCode::LGT,  OpCode::FGT,
    OpCode::DGT,  OpCode::IEQ,  OpCode::LEQ,  OpCode::FEQ,  OpCode::DEQ,  OpCode::INE,
    OpCode::LNE,  OpCode::FNE,  OpCode::DNE,  OpCode::ILE,  OpCode::LLE,  OpCode::FLE,
    OpCode::DLE,  OpCode::IGE,  OpCode::LGE,  OpCode::FGE,  OpCode::DGE};

// 映射表：OpCode → DenseOpCode
// 用256大小的查找表直接映射，未定义映射值可设置为 COUNT（表示非法）
static constexpr std::array<DenseOpCode, 256> OpCodeToDenseTable = [] {
    std::array<DenseOpCode, 256> table{};
    table.fill(DenseOpCode::COUNT); // 默认非法
    for (size_t i = 0; i < DenseToOpCodeTable.size(); ++i) {
        auto oc   = static_cast<uint8_t>(DenseToOpCodeTable[i]);
        table[oc] = static_cast<DenseOpCode>(i);
    }
    return table;
}();

constexpr DenseOpCode toDense(OpCode oc) { return OpCodeToDenseTable[static_cast<uint8_t>(oc)]; }

constexpr OpCode fromDense(DenseOpCode doc) {
    if (doc == DenseOpCode::COUNT)
        throw std::out_of_range("Invalid DenseOpCode");
    return DenseToOpCodeTable[static_cast<size_t>(doc)];
}

// inline void makeDense(BytecodeHeader &bc) {
//     bc.opcode = reinterpret_cast<OpCode>(toDense(bc.opcode));
// }

enum class MarkOpCode {
    MapArr,
    ApplyArr,
    ReduceArr,
    FilterArr,
    ForeachArr,
};

inline bool isOpCodeWithFixedOperands(OpCode opcode) {
    return (static_cast<uint8_t>(opcode) & 0b10000000) == 0b00000000;
}

inline bool isOpCodeOfInlinedOperator(OpCode opcode) {
    return (static_cast<uint8_t>(opcode) & 0b01000000) == 0b01000000;
}

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

    bool hasOperands() const { return !isOpCodeWithFixedOperands(opcode); }

    size_t normCnt() const {
        ASSERT(hasOperands(), "No operands available.");
        return static_cast<size_t>(fastop[0]);
    }
    size_t withCnt() const {
        ASSERT(hasOperands(), "No operands available.");
        return static_cast<size_t>(fastop[1]);
    }
    size_t argsCnt() const {
        ASSERT(hasOperands(), "No operands available.");
        return normCnt() + withCnt();
    }

    const data_arr_t nargs() const {
        ASSERT(hasOperands(), "No operands available.");
        return data_arr_t{reinterpret_cast<const data_idx_t *>(this + 1), normCnt()};
    }
    const data_arr_t wargs() const {
        ASSERT(hasOperands(), "No operands available.");
        return data_arr_t{reinterpret_cast<const data_idx_t *>(this + 1) + normCnt(), withCnt()};
    }

    inline data_idx_t *operands() {
        ASSERT(hasOperands(), "No operands available.");
        return reinterpret_cast<data_idx_t *>(this + 1);
    }

    inline const data_idx_t *operands() const {
        ASSERT(hasOperands(), "No operands available.");
        return reinterpret_cast<const data_idx_t *>(this + 1);
    }

    inline BytecodeExtra *extra() { return reinterpret_cast<BytecodeExtra *>(this + opsize - 1); }

    inline const BytecodeExtra *extra() const {
        return reinterpret_cast<const BytecodeExtra *>(this + opsize - 1);
    }
};

union BytecodeExtra {      // 8 bytes
    Type *pType;           // for CAST
    GraphIR::Graph *graph; // for FUNC
    operator_t func;       // for OPER
    MarkOpCode mark;       // for SCHD

    std::string toString(OpCode opcode) const;
};

using Bytecode = BytecodeHeader;

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
