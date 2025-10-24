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
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "bytecode.h"

std::string to_string(const OpCode &op) {
    switch (op) {
    case OpCode::NOOP:
        return "NOOP";
    case OpCode::CAST:
        return "CAST";
    case OpCode::COPY:
        return "COPY";
    case OpCode::FILL:
        return "FILL";
    case OpCode::ACCS:
        return "ACCS";
    case OpCode::BRCH:
        return "BRCH";
    case OpCode::JUMP:
        return "JUMP";
    case OpCode::JOIN:
        return "JOIN";
    case OpCode::CALL:
        return "CALL";
    case OpCode::FUNC:
        return "FUNC";
    case OpCode::OPER:
        return "OPER";
    default:
        ASSERT(false, "Unknown OpCode encountered.");
        return "UNKNOWN";
    }
}

std::string BytecodeHeader::toString() const {
    if (hasOperands()) {
        return std::format("{} ({}) [{}]", to_string(opcode), opsize, formatIndex(result));
    } else {
        return std::format(
            "{} ({}) [{}] | [{}, {}]",
            to_string(opcode),
            opsize,
            formatIndex(result),
            formatIndex(fastop[0]),
            formatIndex(fastop[1]));
    }
}

std::string BytecodeExtra::toString(OpCode opcode) const {
    switch (opcode) {
    case OpCode::CAST:
        return std::format("Type: {}", pType ? pType->toString() : "null");
    case OpCode::FUNC:
        return std::format("Graph: {}", graph ? graph->mangledName() : "null");
    case OpCode::OPER:
        return std::format("Func: {}", reinterpret_cast<void *>(func));
    default:
        return "";
    }
}

void appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, data_idx_t result, const std::vector<data_idx_t> &fastops,
    const std::vector<data_idx_t> &withOperands, const std::vector<data_idx_t> &normOperands,
    bool hasExtra, const BytecodeExtra &extra) {

    data_idx_t normCnt = as_index(normOperands.size());
    data_idx_t withCnt = as_index(withOperands.size());

    size_t operandCount = withCnt + normCnt;

    size_t operandUnits = 0;
    if (fastops.empty()) {
        size_t operandBytes = operandCount * sizeof(data_idx_t);
        size_t paddedOperandBytes = roundUp8(operandBytes);
        operandUnits = paddedOperandBytes / sizeof(Bytecode);
    }

    size_t totalUnits = 1 + operandUnits + (hasExtra ? 1 : 0);

    // 获取当前 vec 的插入起始位置
    size_t offset = vec.size();
    vec.resize(offset + totalUnits);

    // Step 1: 写入 Header
    BytecodeHeader *header = reinterpret_cast<BytecodeHeader *>(&vec[offset]);
    header->opcode = opcode;
    header->opsize = static_cast<uint8_t>(totalUnits);
    header->result = result;

    // Step 2: 写入 Operands（如果没有 fastop）
    if (!fastops.empty()) {
        header->fastop[0] = fastops[0];
        if (fastops.size() > 1) {
            header->fastop[1] = fastops[1];
        }
    } else {
        header->fastop[0] = withCnt;
        header->fastop[1] = normCnt;
        uint8_t *raw = reinterpret_cast<uint8_t *>(&vec[offset + 1]);
        data_idx_t *ops = reinterpret_cast<data_idx_t *>(raw);

        for (data_idx_t idx : withOperands) {
            *ops++ = idx;
        }
        for (data_idx_t idx : normOperands) {
            *ops++ = idx;
        }
    }

    // Step 3: 写入 Extra（如果有）
    if (hasExtra) {
        BytecodeExtra *ex = reinterpret_cast<BytecodeExtra *>(&vec[offset + totalUnits - 1]);
        std::memcpy(ex, &extra, sizeof(BytecodeExtra));
    }
}
