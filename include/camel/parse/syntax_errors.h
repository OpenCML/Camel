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
 * Turns what ANTLR reports about a source file into diagnostics.
 *
 * ANTLR describes an error in terms of its own machinery ("mismatched input 'b' expecting
 * {',', ')'}", "no viable alternative at input '((1+2)return'"). The listeners here classify
 * the common mistakes (an unclosed bracket, a missing `then`, `=` in a condition, a keyword
 * used as a name, ...) and report each under a code of its own with a message about the
 * program; what is not recognised is still reported, with the token names made readable.
 *
 * Only the first syntax error of a file is reported. After an error the parser resynchronises
 * by guessing, and what it reports from then on is mostly a consequence of the guess, not a
 * second mistake. Lexical errors (a character that starts no token, a string without its
 * closing quote) are independent of one another and are all reported; a file that has one is
 * not also given a syntax error, since the parser saw the tokens without the offending text.
 */

#pragma once

#include "antlr4-runtime/antlr4-runtime.h"
#include "camel/core/error/diagnostics.h"

namespace camel::parse {

class LexerErrorListener : public antlr4::BaseErrorListener {
  public:
    explicit LexerErrorListener(camel::core::error::diagnostics_ptr_t diagnostics)
        : diagnostics_(std::move(diagnostics)) {}

    void syntaxError(
        antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
        size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override;

  private:
    camel::core::error::diagnostics_ptr_t diagnostics_;
    // Where the previous error ended, to report a run of illegal characters once.
    size_t lastLine_   = 0;
    size_t lastColumn_ = 0;
    bool any_          = false;
};

class ParserErrorListener : public antlr4::BaseErrorListener {
  public:
    explicit ParserErrorListener(camel::core::error::diagnostics_ptr_t diagnostics)
        : diagnostics_(std::move(diagnostics)) {}

    bool hasErrors() const { return hasErrors_; }

    void syntaxError(
        antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
        size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override;

  private:
    bool hasErrors_ = false;
    camel::core::error::diagnostics_ptr_t diagnostics_;
};

} // namespace camel::parse
