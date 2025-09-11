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
#include "diag_type.h"
#include "diag_utils.h"
#include "specific/lexical_diag_info.h"
#include "specific/syntax_diag_info.h"

inline DiagInfo getDiagInfo(DiagType type, uint32_t specific) {
  switch (type) {
  case DiagType::LexicalDiag: {
    const auto &map = getLexicalDiagInfoMap();
    auto it = map.find(static_cast<LexicalDiag>(specific));
    if (it != map.end()) {
      return it->second;
    }
    break;
  }
  case DiagType::SyntaxDiag: {
    const auto &map = getSyntaxDiagInfoMap();
    auto it = map.find(static_cast<SyntaxDiag>(specific));
    if (it != map.end()) {
      return it->second;
    }
    break;
  }
  default:
    break;
  }
  return DiagInfo{"Unknown", "Unknown diagnostic", ""};
}

inline DiagInfo getDiagInfo(uint32_t diagCode) {
  DiagType type = extractDiagType(diagCode);
  uint32_t specific = extractSpecific(diagCode);
  return getDiagInfo(type, specific);
}

template <typename ErrorEnum> DiagInfo getDiagInfo(ErrorEnum err) {
  DiagType type = diagTypeOf(err);
  uint32_t specific = static_cast<uint32_t>(err);
  return getDiagInfo(type, specific);
}