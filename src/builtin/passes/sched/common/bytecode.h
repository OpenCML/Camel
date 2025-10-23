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
 * Updated: Oct. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <array>
#include <cstdint>

#include "compile/gir.h"
#include "utils/align.h"

constexpr size_t CacheLineSize = 64;
constexpr size_t ByteCodeSize = CacheLineSize / 2;

enum class OpCode : uint8_t {
    CAST, // index
    COPY,
    FILL,
    ACCS,
    BRCH,
    JUMP,
    JOIN,
    CALL,
    FUNC,
    OPER,
};

std::string to_string(const OpCode &op);

struct alignas(ByteCodeSize) Bytecode {
    OpCode opcode;                  // 1 byte
    uint8_t self;                   // 1 byte
    uint8_t withCnt;                // 1 byte
    uint8_t normCnt;                // 1 byte
    std::array<uint8_t, 20> inputs; // 20 bytes

    union Extra {              // 8 bytes
        Type *pType;           // for CAST
        size_t target;         // for JUMP
        GraphIR::Graph *graph; // for FUNC
        operator_t func;       // for OPER
        size_t index;          // for ACCS
    } extra;

    std::string toString() const;
    std::string extraStr() const;
};

static_assert(sizeof(Bytecode) == ByteCodeSize, "Bytecode must be exactly 32 bytes");

using bytecode_vec_t = std::vector<Bytecode, AlignedAllocator<Bytecode, ByteCodeSize>>;
