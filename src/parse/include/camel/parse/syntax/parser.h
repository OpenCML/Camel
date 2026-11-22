// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    /// Parses one expression and nothing else (the content of a `${...}` of a formatted
    /// string). The token buffer may cover a range of a larger source; the tree's nodes then
    /// have the positions of that source. The tree has errors where the text is not one
    /// expression, and its root is the expression node.
    static Tree parseExpression(TokenBuffer tokens);
};

} // namespace camel::parse::syntax
