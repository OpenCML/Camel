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
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "diag_type.h"

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

inline int severityToDiagnosticInt(Severity s) {
  switch (s) {
  case Severity::Error:
    return 1;
  case Severity::Warning:
    return 2;
  case Severity::Info:
    return 3;
  case Severity::Hint:
    return 4;
  default:
    return 0xF;
  }
}

inline const char *severityToString(Severity s) {
  switch (s) {
  case Severity::Error:
    return "Error";
  case Severity::Warning:
    return "Warning";
  case Severity::Info:
    return "Info";
  case Severity::Hint:
    return "Hint";
  default:
    return "Unknown";
  }
}