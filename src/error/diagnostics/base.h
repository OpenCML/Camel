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
 * Created: Sep. 11, 2025
 * Updated: Sep. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <string>

/*
 * 32-bit full error code layout (runtime):
 *  0x TT SSSSSS
 *      |   \---- specificCode (24 bits) high nibble = severity (0..3 used)
 *      |
 *      \-------- error type (8 bits)
 *
 * Severity nibble (specificCode high 4 bits):
 *   0x0 = Error
 *   0x1 = Warning
 *   0x2 = Info
 *   0x3 = Hint
 */

enum class DiagType : uint8_t {
    InternalDiag = 0x00,
    LexicalDiag = 0x01,
    SyntaxDiag = 0x02,
    SemanticDiag = 0x03,
    RuntimeDiag = 0x04,
};

enum class Severity : uint8_t { Error = 0, Warning = 1, Info = 2, Hint = 3, Unknown = 0xF };

std::string to_string(Severity s);

inline uint32_t makeDiagCode(DiagType type, uint32_t specificCode) {
    return (static_cast<uint32_t>(type) << 24) | (specificCode & 0xFFFFFF);
}

inline DiagType extractDiagType(uint32_t nonModuleKey) {
    return static_cast<DiagType>((nonModuleKey >> 24) & 0xFF);
}

inline uint32_t extractSpecific(uint32_t nonModuleKey) {
    return static_cast<uint32_t>(nonModuleKey & 0xFFFFFF);
}

inline Severity extractSeverity(uint32_t nonModuleKey) {
    return static_cast<Severity>((nonModuleKey >> 20) & 0xF);
}

struct DiagInfo {
    std::string name;
    std::string message;
    std::string suggestion;

    DiagInfo() = default;
    DiagInfo(const std::string &name, const std::string &msg, const std::string &sug)
        : name(name), message(msg), suggestion(sug) {}
};

DiagInfo getDiagInfo(DiagType type, uint32_t specific);

DiagInfo getDiagInfo(uint32_t diagCode);

template <typename ErrorEnum> DiagInfo getDiagInfo(ErrorEnum err) {
    DiagType type = diagTypeOf(err);
    uint32_t specific = static_cast<uint32_t>(err);
    return getDiagInfo(type, specific);
}