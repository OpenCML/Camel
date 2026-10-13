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
};

} // namespace camel::parse::syntax
