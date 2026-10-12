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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "antlr4-runtime/antlr4-runtime.h"
#include "camel/core/source/ids.h"

#include <string>
#include <variant>

namespace camel::core::error {

// ---- Basic structures ----
struct CharPos {
    size_t line      = 0;
    size_t character = 0;
};

struct CharRange {
    CharPos start;
    CharPos end;

    static CharRange fromTokens(antlr4::Token *start, antlr4::Token *end);
};

struct TokenRange {
    size_t start = 0;
    size_t end   = 0;
};

using SourceRange = std::variant<
    std::monostate, CharRange, TokenRange, camel::source::span_id_t, camel::source::origin_id_t>;

/// A secondary location of a diagnostic: a place that explains the primary one ("the parameter
/// is declared here", "instantiated by this call"). `message` reads as a sentence fragment that
/// the location completes; the one-line text format prints it as " [message at line:col]".
struct DiagLabel {
    SourceRange range{};
    std::string message;
    /// File of `range` when it is a CharRange in another file than the diagnostic's; a span or
    /// an origin names its own file.
    std::string path;
};

/// Where the tokens of a file are: what turns a TokenRange into lines and columns. The parser
/// that produced the tokens provides it (camel/parse/parse.h).
class TokenPositions {
  public:
    virtual ~TokenPositions() = default;
    virtual size_t size() const = 0;
    /// From the first character of token `first` to the end of token `last`.
    virtual CharRange charRange(size_t first, size_t last) const = 0;
};

// ---- RangeConverter ----
class RangeConverter {
  public:
    RangeConverter(const TokenPositions &tokens) : tokens_(tokens) {}
    CharRange conv(const TokenRange &tr) const;

  private:
    const TokenPositions &tokens_;
};

} // namespace camel::core::error
