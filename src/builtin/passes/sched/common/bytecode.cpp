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

#include "bytecode.h"

std::string to_string(const OpCode &op) {
    switch (op) {
    case OpCode::RETN:
        return "RETN";
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
    case OpCode::TAIL:
        return "TAIL";
    case OpCode::OPER:
        return "OPER";
    case OpCode::SCHD:
        return "SCHD";

    case OpCode::IADD:
        return "IADD";
    case OpCode::LADD:
        return "LADD";
    case OpCode::FADD:
        return "FADD";
    case OpCode::DADD:
        return "DADD";

    case OpCode::ISUB:
        return "ISUB";
    case OpCode::LSUB:
        return "LSUB";
    case OpCode::FSUB:
        return "FSUB";
    case OpCode::DSUB:
        return "DSUB";

    case OpCode::IMUL:
        return "IMUL";
    case OpCode::LMUL:
        return "LMUL";
    case OpCode::FMUL:
        return "FMUL";
    case OpCode::DMUL:
        return "DMUL";

    case OpCode::IDIV:
        return "IDIV";
    case OpCode::LDIV:
        return "LDIV";
    case OpCode::FDIV:
        return "FDIV";
    case OpCode::DDIV:
        return "DDIV";

    case OpCode::ILT:
        return " ILT";
    case OpCode::LLT:
        return " LLT";
    case OpCode::FLT:
        return " FLT";
    case OpCode::DLT:
        return " DLT";

    case OpCode::IGT:
        return " IGT";
    case OpCode::LGT:
        return " LGT";
    case OpCode::FGT:
        return " FGT";
    case OpCode::DGT:
        return " DGT";

    case OpCode::IEQ:
        return " IEQ";
    case OpCode::LEQ:
        return " LEQ";
    case OpCode::FEQ:
        return " FEQ";
    case OpCode::DEQ:
        return " DEQ";

    case OpCode::INE:
        return " INE";
    case OpCode::LNE:
        return " LNE";
    case OpCode::FNE:
        return " FNE";
    case OpCode::DNE:
        return " DNE";

    case OpCode::ILE:
        return " ILE";
    case OpCode::LLE:
        return " LLE";
    case OpCode::FLE:
        return " FLE";
    case OpCode::DLE:
        return " DLE";

    case OpCode::IGE:
        return " IGE";
    case OpCode::LGE:
        return " LGE";
    case OpCode::FGE:
        return " FGE";
    case OpCode::DGE:
        return " DGE";

    default:
        ASSERT(false, "Unknown OpCode encountered.");
        return "UNKNOWN";
    }
}

std::string to_string(const MarkOpCode &op) {
    switch (op) {
    case MarkOpCode::MapArr:
        return "MapArr";
    case MarkOpCode::ApplyArr:
        return "ApplyArr";
    case MarkOpCode::ReduceArr:
        return "ReduceArr";
    case MarkOpCode::FilterArr:
        return "FilterArr";
    case MarkOpCode::ForeachArr:
        return "ForeachArr";
    default:
        ASSERT(false, "Unknown MarkOpCode encountered.");
        return "UNKNOWN";
    }
}

std::string BytecodeHeader::toString() const {
    return std::format(
        "{} ({}) [{}] | [{}, {}]",
        to_string(opcode),
        opsize,
        formatIndex(result),
        formatIndex(fastop[0]),
        formatIndex(fastop[1]));
}

std::string BytecodeExtra::toString(OpCode opcode) const {
    switch (opcode) {
    case OpCode::CAST:
        return std::format("{}", pType ? pType->toString() : "null");
    case OpCode::FUNC:
        [[fallthrough]];
    case OpCode::TAIL:
        return std::format("{}", graph ? graph->mangledName() : "null");
    case OpCode::OPER:
        return std::format("{}", reinterpret_cast<void *>(func));
    case OpCode::SCHD:
        return std::format("{}", to_string(mark));
    default:
        return "";
    }
}

Bytecode *appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, data_idx_t result, const std::vector<data_idx_t> &fastops,
    const std::vector<data_idx_t> &normOperands, const std::vector<data_idx_t> &withOperands,
    bool hasExtra, const BytecodeExtra &extra) {

    data_idx_t normCnt = as_index(normOperands.size());
    data_idx_t withCnt = as_index(withOperands.size());

    size_t operandCount = normCnt + withCnt;

    size_t operandUnits = 0;
    if (fastops.empty()) {
        size_t operandBytes       = operandCount * sizeof(data_idx_t);
        size_t paddedOperandBytes = roundUp8(operandBytes);
        operandUnits              = paddedOperandBytes / sizeof(Bytecode);
    }

    size_t totalUnits = 1 + operandUnits + (hasExtra ? 1 : 0);

    // 获取当前 vec 的插入起始位置
    size_t offset = vec.size();
    vec.resize(offset + totalUnits);

    // Step 1: 写入 Header
    BytecodeHeader *header = reinterpret_cast<BytecodeHeader *>(&vec[offset]);
    header->opcode         = opcode;
    header->opsize         = static_cast<uint8_t>(totalUnits);
    header->result         = result;

    // Step 2: 写入 Operands（如果没有 fastop）
    if (!fastops.empty()) {
        header->fastop[0] = fastops[0];
        if (fastops.size() > 1) {
            header->fastop[1] = fastops[1];
        }
    } else {
        header->fastop[0] = normCnt;
        header->fastop[1] = withCnt;
        uint8_t *raw      = reinterpret_cast<uint8_t *>(&vec[offset + 1]);
        data_idx_t *ops   = reinterpret_cast<data_idx_t *>(raw);

        for (data_idx_t idx : normOperands) {
            *ops++ = idx;
        }
        for (data_idx_t idx : withOperands) {
            *ops++ = idx;
        }
    }

    // Step 3: 写入 Extra（如果有）
    if (hasExtra) {
        BytecodeExtra *ex = reinterpret_cast<BytecodeExtra *>(&vec[offset + totalUnits - 1]);
        *ex               = extra;
    }

    return header;
}
