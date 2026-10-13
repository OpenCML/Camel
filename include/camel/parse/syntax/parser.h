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
 * The parser: hand-written recursive descent over the token buffer, one function per production
 * of the grammar (docs/language/08_top_level_and_grammar.md), no backtracking.
 *
 * Every decision is made on the current token and at most four tokens after it; what each
 * decision looks at is written at the function that makes it and collected in
 * docs/technical/34_handwritten_parser.md. The two layout rules (angle brackets are adjacent,
 * a line break ends a statement) are decided on the flags the lexer left on the tokens.
 *
 * A source text always yields a tree. Where the text is wrong the parser records a SyntaxError,
 * leaves out what is missing, wraps what it has to skip in an Error node and goes on at the
 * next statement, so that several independent mistakes are each found once.
 */

#pragma once

#include "cst.h"

namespace camel::parse::syntax {

class Parser {
  public:
    /// Lexes and parses `source`.
    static Tree parse(std::string source);
    /// Parses an already lexed buffer.
    static Tree parse(TokenBuffer tokens);
};

} // namespace camel::parse::syntax
