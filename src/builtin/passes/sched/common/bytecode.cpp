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

#include "bytecode.h"

std::string to_string(const OpCode &op) {
    switch (op) {
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

std::string Bytecode::toString() const {
    std::string withInputsStr = "[";
    for (size_t i = 0; i < withCnt; ++i) {
        withInputsStr += std::to_string(inputs[i]);
        if (i < withCnt - 1) {
            withInputsStr += ", ";
        }
    }
    withInputsStr += "]";

    std::string normInputsStr = "[";
    for (size_t i = withCnt; i < withCnt + normCnt; ++i) {
        normInputsStr += std::to_string(inputs[i]);
        if (i < withCnt + normCnt - 1) {
            normInputsStr += ", ";
        }
    }
    normInputsStr += "]";

    return std::format(
        "{}(self: {}, with: {}, norm: {})",
        to_string(opcode),
        self == 255 ? "\\N" : std::to_string(self),
        withInputsStr,
        normInputsStr);
}

std::string Bytecode::extraStr() const {
    switch (opcode) {
    case OpCode::CAST:
        return std::format("type: {}", extra.pType->toString());
    case OpCode::JUMP:
        return std::format("target: {}", extra.target);
    case OpCode::FUNC:
        return std::format("graph: {}", extra.graph->mangledName());
    case OpCode::OPER:
        return std::format("func: {}", reinterpret_cast<void *>(extra.func));
    case OpCode::ACCS:
        return std::format("index: {}", extra.index);
    default:
        return "";
    }
}
