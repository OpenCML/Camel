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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "compile/gir.h"
#include "utils/rawarr.h"

enum class OpCode : uint8_t {
    // 定长参数指令
    NOOP = 0b00000000, // no operation
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
};

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
    OpCode opcode        = OpCode::NOOP; // 1 byte
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
