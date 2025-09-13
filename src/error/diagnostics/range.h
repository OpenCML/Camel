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
 * Created: Sep. 06, 2025
 * Updated: Sep. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "antlr4-runtime/antlr4-runtime.h"

#include <variant>

// ---- Basic structures ----
struct CharPos {
    size_t line = 0;
    size_t character = 0;
};

struct CharRange {
    CharPos start;
    CharPos end;

    static CharRange fromTokens(antlr4::Token *start, antlr4::Token *end);
};

struct TokenRange {
    size_t start = 0;
    size_t end = 0;
};

using SourceRange = std::variant<std::monostate, CharRange, TokenRange>;

// ---- RangeConverter ----
class RangeConverter {
  public:
    RangeConverter(const std::vector<antlr4::Token *> &toks) : tokens_(toks) {}
    CharRange conv(const TokenRange &tr) const;

  private:
    const std::vector<antlr4::Token *> &tokens_;
};