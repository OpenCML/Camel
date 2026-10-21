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
    case OpCode::LBEG:
        return "LBEG";
    case OpCode::LTST:
        return "LTST";
    case OpCode::LEND:
        return "LEND";

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

    case OpCode::IMOD:
        return "IMOD";
    case OpCode::LMOD:
        return "LMOD";

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
    case MarkOpCode::ReduceArr:
        return "ReduceArr";
    case MarkOpCode::FilterArr:
        return "FilterArr";
    case MarkOpCode::ForeachArr:
        return "ForeachArr";
    case MarkOpCode::Isolate:
        return "Isolate";
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
        formatIndex(result, 2),
        formatIndex(fastop[0], 2),
        formatIndex(fastop[1], 2));
}

std::string BytecodeExtra::toString(OpCode opcode) const {
    switch (opcode) {
    case OpCode::CAST:
        return std::format("{}", pType ? pType->toString() : "null");
    case OpCode::FUNC:
        [[fallthrough]];
    case OpCode::TAIL:
        return std::format("{}", reinterpret_cast<const void *>(raw));
    case OpCode::OPER:
        return std::format("{}", reinterpret_cast<void *>(func));
    case OpCode::SCHD:
        return std::format("{}", to_string(mark));
    case OpCode::FILL:
        return std::format("{}", pType ? pType->toString() : "null");
    default:
        return "";
    }
}

Bytecode *appendBytecode(
    bytecode_vec_t &vec, OpCode opcode, data_idx_t result, const std::vector<data_idx_t> &fastops,
    const std::vector<data_idx_t> &normOperands, const std::vector<data_idx_t> &withOperands,
    bool hasExtra, const BytecodeExtra &extra, size_t extraWords) {

    data_idx_t normCnt = as_index(normOperands.size());
    data_idx_t withCnt = as_index(withOperands.size());

    size_t operandCount = static_cast<size_t>(normCnt) + static_cast<size_t>(withCnt);

    size_t operandUnits = 0;
    if (fastops.empty()) {
        size_t operandBytes       = operandCount * sizeof(data_idx_t);
        size_t paddedOperandBytes = roundUp16(operandBytes);
        operandUnits              = paddedOperandBytes / sizeof(Bytecode);
    }

    // The extra words are 8 bytes each and end where the instruction ends.
    const size_t extraUnits =
        hasExtra ? (extraWords * sizeof(BytecodeExtra) + sizeof(Bytecode) - 1) / sizeof(Bytecode)
                 : 0;
    size_t totalUnits = 1 + operandUnits + extraUnits;
    if (totalUnits > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error(
            std::format("FastVM: an instruction of {} units does not fit a bytecode", totalUnits));
    }

    // Get the insertion start position in the current vector.
    size_t offset = vec.size();
    vec.resize(offset + totalUnits);

    // Step 1: write the header.
    BytecodeHeader *header = reinterpret_cast<BytecodeHeader *>(&vec[offset]);
    header->opcode         = opcode;
    header->opsize         = static_cast<uint16_t>(totalUnits);
    header->result         = result;

    // Step 2: write operands (if there are no fastops).
    if (!fastops.empty()) {
        header->fastop[0] = fastops[0];
        if (fastops.size() > 1) {
            header->fastop[1] = fastops[1];
        }
    } else {
        header->fastop[0] = normCnt;
        header->fastop[1] = withCnt;
        data_idx_t *ops   = header->operands();

        for (data_idx_t idx : normOperands) {
            *ops++ = idx;
        }
        for (data_idx_t idx : withOperands) {
            *ops++ = idx;
        }
    }

    // Step 3: write extra data (if present).
    if (hasExtra) {
        BytecodeExtra *ex0 = header->extra();
        *ex0               = extra;
        for (size_t i = 1; i < extraWords; i++) {
            *(reinterpret_cast<uint64_t *>(ex0) + i) = 0;
        }
    }

    return header;
}
