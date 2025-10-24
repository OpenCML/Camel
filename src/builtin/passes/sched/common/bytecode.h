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
 * Updated: Oct. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "compile/gir.h"

enum class OpCode : uint8_t {
    NOOP, // no operation
    CAST,
    COPY, // 使用 fastop[0] 作为待拷贝索引
    FILL,
    ACCS, // fastop[0] 作为目标索引，fastop[1] 作为访问的下标
    BRCH,
    JUMP, // 使用 fastop[0] 作为跳转目标地址
    JOIN,
    CALL,
    FUNC,
    OPER,
    SCHD, // 使用 fastop[0] 作为调度策略 ID
};

// 0 代表空，正数表示动态数据段索引，负数表示静态数据段索引的相反数
using index_t = int16_t;

inline bool isSafeSizeTForIndexT(size_t value) {
    return value <= static_cast<size_t>(std::numeric_limits<int16_t>::max());
}

inline bool isSafeInt64ForIndexT(int64_t value) {
    return value >= static_cast<int64_t>(std::numeric_limits<index_t>::min()) &&
           value <= static_cast<int64_t>(std::numeric_limits<index_t>::max());
}

template <typename T> inline index_t as_index(T value) {
    if constexpr (std::is_same_v<T, size_t>) {
        ASSERT(
            isSafeSizeTForIndexT(value),
            "Value exceeds int16_t range when converting to index_t.");
    } else if constexpr (std::is_same_v<T, int64_t>) {
        ASSERT(
            isSafeInt64ForIndexT(value),
            "Value exceeds int16_t range when converting to index_t.");
    } else {
        static_assert(false, "Unsupported type for index_t conversion.");
    }
    return static_cast<index_t>(value);
}

inline std::string formatIndex(index_t value) {
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

struct BytecodeHeader;
struct BytecodeOperands;
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
// | withCnt    : 1 byte         |
// | normCnt    : 1 byte         |
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

struct BytecodeHeader {           // 8 bytes
    OpCode opcode = OpCode::NOOP; // 1 byte
    uint8_t opsize = 0;           // 1 byte，单位为 8 字节
    index_t result = 0;           // 2 bytes
    index_t fastop[2] = {0, 0};   // 4 bytes

    std::string toString() const;

    bool hasOperands() const { return !(fastop[0] != 0 || fastop[1] != 0); }

    inline BytecodeOperands *operands() { return reinterpret_cast<BytecodeOperands *>(this + 1); }

    inline const BytecodeOperands *operands() const {
        // Header 占 1 个 8 字节单位
        return reinterpret_cast<const BytecodeOperands *>(this + 1);
    }

    inline BytecodeExtra *extra() { return reinterpret_cast<BytecodeExtra *>(this + opsize - 1); }

    inline const BytecodeExtra *extra() const {
        // Extra 位于 Bytecode 的最后一个 8 字节单位
        return reinterpret_cast<const BytecodeExtra *>(this + opsize - 1);
    }
};

struct BytecodeOperands { // 2 bytes at least
    uint8_t withCnt = 0;  // 1 byte
    uint8_t normCnt = 0;  // 1 byte
    index_t operands[];   // variable length

    std::string toString() const;
};

union BytecodeExtra {      // 8 bytes
    Type *pType;           // for CAST
    GraphIR::Graph *graph; // for FUNC
    operator_t func;       // for OPER

    std::string toString(OpCode opcode) const;
};

using Bytecode = BytecodeHeader;

static_assert(sizeof(Bytecode) == 8, "Bytecode must be exactly 8 bytes");
static_assert(sizeof(BytecodeHeader) == 8, "BytecodeHeader must be exactly 8 bytes");
static_assert(sizeof(BytecodeExtra) == 8, "BytecodeExtra must be exactly 8 bytes");
static_assert(sizeof(BytecodeOperands) == 2, "BytecodeOperands must be exactly 2 bytes");

using bytecode_vec_t = std::vector<Bytecode>;

inline size_t roundUp8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

void appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, index_t result, const std::vector<index_t> &fastops = {},
    const std::vector<index_t> &withOperands = {}, const std::vector<index_t> &normOperands = {},
    bool hasExtra = false, const BytecodeExtra &extra = {});
