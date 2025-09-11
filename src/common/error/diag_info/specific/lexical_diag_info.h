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
#include <unordered_map>

#include "../diag_type.h"

enum class LexicalDiag : uint32_t {
  // severity = 0 (Error)
  IllegalCharacter = 0x000000,
  InvalidNumberFormat = 0x000001,
  UnclosedStringLiteral = 0x000002,
  UnterminatedComment = 0x000003,

  // severity = 1 (Warning)
  InvalidEscapeSequence = 0x010000,
};

constexpr DiagType diagTypeOf(LexicalDiag) { return DiagType::LexicalDiag; }

const std::unordered_map<LexicalDiag, DiagInfo> getLexicalDiagInfoMap();