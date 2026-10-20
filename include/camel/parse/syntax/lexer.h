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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The lexer: one pass over the bytes of a source text, no backtracking over more than the
 * token in hand, no allocation per token.
 *
 * What it accepts is the lexical grammar of the language reference (chapter 2); the decisions
 * that needed care are listed in docs/technical/34_handwritten_parser.md, section "Lexer".
 */

#pragma once

#include "token.h"

namespace camel::parse::syntax {

class Lexer {
  public:
    /// Tokenizes `source`. The buffer ends with an Eof token; lexical errors are recorded in
    /// the buffer (TokenBuffer::errors) and their text is kept as Error tokens, which the
    /// parser skips like comments.
    static TokenBuffer tokenize(std::string source);

    /// Tokenizes only `source[begin, end)` (used for the expression inside a `${...}` of a
    /// formatted string). The buffer still holds the whole source, so the byte offsets of the
    /// tokens and the line / column arithmetic of the buffer are those of the file: a tree
    /// parsed from it places its nodes where they were written. Line starts before and after
    /// the range are kept, too.
    static TokenBuffer tokenizeRange(std::string source, size_t begin, size_t end);

  private:
    /// The one pass: `from`/`to` bound the bytes lexed (the whole text otherwise).
    static TokenBuffer tokenizeImpl(std::string source, size_t from, size_t to);
};

} // namespace camel::parse::syntax
