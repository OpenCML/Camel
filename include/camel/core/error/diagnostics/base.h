// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace camel::core::error {

/*
 * 32-bit full error code layout:
 * Bit Index:  31        24 23       20 19                         0
 *             |<-- 8 -->|<-- 4 -->|<-------- 20 bits ----------->|
 *             +---------+---------+------------------------------+
 *             |  Type   | Severity|      Specific Code           |
 *             +---------+---------+------------------------------+
 */

enum class DiagType : uint8_t {
    InternalDiag = 0x00,
    LexicalDiag  = 0x01,
    SyntaxDiag   = 0x02,
    SemanticDiag = 0x03,
    RuntimeDiag  = 0x04,
};

std::string to_string(DiagType type);

enum class Severity : uint8_t { Error = 0x0, Warn = 0x1, Info = 0x2, Hint = 0x3, Unknown = 0xF };

std::string to_string(Severity s);
std::string to_colorful_string(Severity s);

inline uint32_t makeDiagCode(DiagType type, uint32_t specificCode) {
    return (static_cast<uint32_t>(type) << 24) | (specificCode & 0xFFFFFF);
}

inline DiagType extractDiagType(uint32_t key) { return static_cast<DiagType>((key >> 24) & 0xFF); }

inline uint32_t extractSpecific(uint32_t key) { return static_cast<uint32_t>(key & 0xFFFFFF); }

inline Severity extractSeverity(uint32_t key) { return static_cast<Severity>((key >> 20) & 0xF); }

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

/// One entry of the diagnostics catalog: a code with its name, message template and suggestion.
struct DiagCatalogEntry {
    DiagType type{};
    uint32_t code{}; // the full 32-bit code
    Severity severity{};
    std::string name; // without the type prefix
    std::string message;
    std::string suggestion;
};

/// Every diagnostic the compiler and the runtime can report, ordered by code. This is what
/// `camel --explain` and the command-line warning names are checked against.
const std::vector<DiagCatalogEntry> &diagnosticCatalog();

template <typename ErrorEnum> DiagInfo getDiagInfo(ErrorEnum err) {
    DiagType type     = diagTypeOf(err);
    uint32_t specific = extractSpecific(static_cast<uint32_t>(err));
    return getDiagInfo(type, specific);
}

} // namespace camel::core::error
